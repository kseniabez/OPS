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
    if(c == 0) return 0;
    if (buf[0] - '0' != port) return -1;

    c = read(fd, buf, 1); // Read sender's address
    if (c < 0) return -1;
    *dest = buf[0] - '0';

    int count = 0;
    do {
        c = read(fd, &message[len], 1); 
        if (c < 0) return -1;
        if (c == 0) return -1;
        len++;
        count++;
    } while (message[len - 1] != '$' && len < MAX_MESSAGE_SIZE);

    message[len-1] = '\0';
    return len -1;
}
void send_message(int* client_sockets, int port, int dest, char* message)
{
        char error[MAX_MESSAGE_SIZE];
        sprintf(error,"0%d Unknown host - %d\n", port, dest);
        int cond = client_sockets[dest];
        if(cond!=0)
        {
            if (write(client_sockets[dest], message, strlen(message) + 1) < 0)
            {
                if(errno== EPIPE)
                {
                    client_sockets[dest] = 0;
                    printf("Disconnect port %d\n", dest);
                    cond = 0;
                }
                else ERR("write:");
            }
            if (cond!= 0 && write(client_sockets[dest], "\n", 2) < 0)
            {
                if(errno== EPIPE)
                {
                    client_sockets[dest] = 0;
                    printf("Disconnect port %d\n", dest);
                    cond = 0;
                    }
                    else ERR("write:");
                }
        }
        if(cond==0)
        {
            printf("%d does not exist.\n", dest);
            if (write(client_sockets[port], error, strlen(error) + 1) < 0)
            {
                if(errno== EPIPE)
                {
                    client_sockets[port] = 0;
                    printf("Disconnect port %d\n", port);
                    cond = 0;
                    return;
                }
                else ERR("write:");
            }
        }
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
            for (int n = 0; n < nfds && do_work; n++)
            {
                memset(buf, 0, MAX_MESSAGE_SIZE);
                int fd = events[n].data.fd;
                int port = isConnected(client_sockets, fd);
                if (port == -1 && fd == tcp_listen_socket) //new connection
                {
                    printf("add_new_client(%d)\n", fd);
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

                    if (response_address > 0 && response_address <= MAX_HOSTS && client_sockets[response_address] == 0)
                    {
                        client_sockets[response_address] = client_socket;
                        sprintf(response, "%d\n", response_address);
                        if (write(client_socket, response, strlen(response) + 1) < 0)
                        {
                                if(errno!= EPIPE) ERR("write:");
                        }
                        printf("Connected to port %d\n", response_address);
                    }
                    else
                    {
                        response_address = -1;
                        sprintf(response, "Wrong Address\n");
                        if (write(client_socket, response, strlen(response) + 1) < 0)
                        {
                            if(errno!= EPIPE) ERR("write:");
                        }
                        if (TEMP_FAILURE_RETRY(close(client_socket)) < 0)
                            ERR("close");
                    }
                }
                else //message from existing connection
                {
                    int dest;
                    int c;
                    if((c = read_message(fd, buf, port, &dest)) <= 0)
                    {
                        if(c == 0)
                        {
                            client_sockets[port] = 0;
                            printf("Disconnect port %d\n", port);
                            epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, fd, NULL);
                            close(fd);
                        }
                        continue;
                    }
                    else
                    {
                        printf("%d -> %d : %s\n", port, dest, buf);
                        if(dest == MAX_HOSTS+1)
                        {
                            for(int j=1; j <= MAX_HOSTS; j++)
                            {
                                send_message(client_sockets, port, j, buf);
                            }
                        }
                        else send_message(client_sockets, port, dest, buf);
                        
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
