#include "l4_common.h"

#define BACKLOG 3
#define MAX_EVENTS 16
#define MAX_HOSTS 8
#define MAX_MESSAGE_SIZE 128

volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig) { do_work = 0; }

void usage(char *name) { fprintf(stderr, "USAGE: %s port\n", name); }

int isConnected(int* client_sockets, int socket)
{
    for(int i=1; i<=MAX_HOSTS; i++)
    {
        if(client_sockets[i]==socket) return i;
    }
    return -1;
}

int read_message(int fd, char* message, int port, int* dest) {
    int c;
    char buf[MAX_MESSAGE_SIZE];
    ssize_t len = 0;

    c = read(fd, buf, 1); // Read recipient's address
    if (c < 0) return -1;
    if (c == 0 || buf[0] - '0' != port) return -1;

    c = read(fd, buf, 1); // Read sender's address
    if (c < 0) return -1;
    *dest = buf[0] - '0';

    int count = 0;
    do {
        c = read(fd, &message[len], 1); 
        if (c < 0) return -1;
        if (c == 0) return -1;
        printf("%d:%c\n", count, message[len]);
        len++;
        count++;
    } while (message[len - 1] != '$' && len < MAX_MESSAGE_SIZE);

    message[len-1] = '\0'; // Null-terminate the message

    return len -1;
}

void doRouter(int tcp_listen_socket)
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, events[MAX_EVENTS];
    event.events = EPOLLIN;

    event.data.fd = tcp_listen_socket;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, tcp_listen_socket, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    int nfds;

    // Array to track assigned addresses, initialized to 0 (unassigned)
    int client_sockets[MAX_HOSTS + 1] = {0};

    sigset_t mask, oldmask;
    char buf[MAX_MESSAGE_SIZE];
    char response[MAX_MESSAGE_SIZE];
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    while (do_work)
    {
        if ((nfds = epoll_pwait(epoll_descriptor, events, MAX_EVENTS, -1, &oldmask)) > 0)
        {
            for (int n = 0; n < nfds; n++)
            {
                memset(buf, 0, MAX_MESSAGE_SIZE);
                int fd = events[n].data.fd;
                int port = isConnected(client_sockets, fd);
                if (port == -1) // New connection
                {
                    int client_socket = add_new_client(fd);
                    event.data.fd = client_socket;
                    event.events = EPOLLIN;
                    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_socket, &event) == -1)
                    {
                        perror("epoll_ctl: client_socket");
                        exit(EXIT_FAILURE);
                    }
                    
                    if (bulk_read(client_socket, buf, 1) < 0)
                        ERR("read:");
                    int response_address = buf[0] - '0';
                    printf("%d\n", response_address);

                    if (response_address > 0 && response_address <= MAX_HOSTS)
                    {
                        client_sockets[response_address] = client_socket;
                        sprintf(response, "%d\n", response_address);
                        if (bulk_write(client_socket, response, strlen(response) + 1) < 0)
                            ERR("write:");
                    }
                    else
                    {
                        response_address = -1;
                        sprintf(response, "Wrong Address\n");
                        if (bulk_write(client_socket, response, strlen(response) + 1) < 0)
                            ERR("write:");
                        if (TEMP_FAILURE_RETRY(close(client_socket)) < 0)
                            ERR("close");
                    }
                }
                else
                {
                    int dest;
                    if(read_message(fd, buf, port, &dest) < 0)
                    {
                        
                    }
                    else
                    {
                        printf("%d -> %d : %s\n", port, dest, buf);
                    }
                }
            }
        }
        else
        {
            if (errno == EINTR)
                continue;
            ERR("epoll_pwait");
        }
    }
    for (int i = 1; i <= MAX_HOSTS; i++)
    {
        if (client_sockets[i] != 0)
        {
            if (TEMP_FAILURE_RETRY(close(client_sockets[i])) < 0)
                ERR("close");
        }
    }
    if (TEMP_FAILURE_RETRY(close(epoll_descriptor)) < 0)
        ERR("close");
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char **argv)
{
    int tcp_listen_socket;
    int new_flags;
    if (argc != 2)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    tcp_listen_socket = bind_tcp_socket(atoi(argv[1]), BACKLOG);
    new_flags = fcntl(tcp_listen_socket, F_GETFL) | O_NONBLOCK;
    fcntl(tcp_listen_socket, F_SETFL, new_flags);
    doRouter(tcp_listen_socket);
    if (TEMP_FAILURE_RETRY(close(tcp_listen_socket)) < 0)
        ERR("close");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}
