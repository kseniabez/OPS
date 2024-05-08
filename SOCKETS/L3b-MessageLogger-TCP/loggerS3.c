#include "l4_common.h"

#define BACKLOG 3
#define MAX_EVENTS 16
#define MAX_CLIENTS 30
#define MAX_MESSAGE_SIZE 128

volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig) { do_work = 0; }

void usage(char *name) { fprintf(stderr, "USAGE: %s port log_file\n", name); }

int isConnected(int* client_sockets, int socket)
{
    for(int i=1; i<=MAX_CLIENTS; i++)
    {
        if(client_sockets[i]==socket) return i;
    }
    return -1;
}
void lowerToUpper(char* message)
{
    for(int i = 0; i < strlen(message); i++)
    {
        if ('A' <= message[i] && message[i] <= 'Z')
        {
            message[i] = message[i] + ('a' - 'A');
        }
        else if ('a' <= message[i] && message[i] <= 'z')
        {
            message[i] = message[i] + ('A' - 'a') ;
        }
    }
}
void underscoerToSpaces(char* message)
{
    for(int i=0; i< strlen(message); i++)
    {
        if(message[i] == ' ')
        {
            message[i] = '_';
        }
        else if(message[i] == '_')
        {
            message[i] = ' ';
        }
    }
}
void filter(char* message, int op)
{
    switch (op)
    {
        case 1:
            lowerToUpper(message);
            break;
        case 2:
            underscoerToSpaces(message);
            break;
        case 3:
            lowerToUpper(message);
            underscoerToSpaces(message);
            break;
        default:
    }
}
void logMessage(int fd, char* message)
{
    char buf[MAX_MESSAGE_SIZE];

    time_t mytime = time(NULL);
    char * time_str = asctime(localtime(&mytime));
    time_str[strlen(time_str)-1] = '\0';

    sprintf(buf, "[%s] %s\n", time_str, message);
    printf("Logging: %s\n", message);
    if(bulk_write(fd, buf, strlen(buf)) < 0) ERR("write");
}
int read_message(int fd, char* message, int* op) {
    int c;
    char buf[MAX_MESSAGE_SIZE];

    c = read(fd, buf, 2); // Read message size
    if (c < 0) return -1;
    if (c == 0) return 0;
    buf[c]= '\0';
    int size = atoi(buf);
    if(size <= 0) return -1;
    c = read(fd, buf, 1); // Read operator
    if (c < 0) return -1;
    if (c == 0) return -1;
    *op = buf[0] - '0';
    printf("Read size: %d\nRead op: %d\n", size, *op);
    if(size <= 0) return -1;
    return bulk_read(fd, message, size);
}

void doLogger(int tcp_listen_socket, int log_file)
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

    int client_sockets[MAX_CLIENTS] = {0};
    int active_clients = 0;

    sigset_t mask, oldmask;
    char wrong_filter[14]="Wrong Filter\n";
    char buf[MAX_MESSAGE_SIZE];
    char message[MAX_MESSAGE_SIZE];
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
                memset(message, 0, MAX_MESSAGE_SIZE);
                int fd = events[n].data.fd;
                int port = isConnected(client_sockets, fd);
                if (port == -1 && fd == tcp_listen_socket) // New connection
                {
                    int client_socket = add_new_client(fd);
                    event.data.fd = client_socket;
                    event.events = EPOLLIN;
                    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_socket, &event) == -1)
                    {
                        perror("epoll_ctl: client_socket");
                        exit(EXIT_FAILURE);
                    }
                    client_sockets[active_clients] = client_socket;
                    active_clients++;
                    
                }
                else // Existing connection
                {
                    int c, op;
                    if((c = read_message(fd, buf, &op)) <= 0)
                    {
                        if (c==0)
                        {
                            epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, fd, NULL);
                            close(fd);
                        }
                        continue;
                    }
                    else
                    {
                        if(op < 1 || op > 3)
                        {
                            printf("Wrong filter\n");
                            if(bulk_write(fd, wrong_filter, strlen(wrong_filter)) < 0)
                            {
                                if(errno == EPIPE) {}
                                else ERR("write");
                            }
                        }
                        else
                        {
                            printf("Got message: %s\n", buf);
                            strcpy(message, buf);
                            filter(message, op);
                            logMessage(log_file, message);
                        }
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
    if (TEMP_FAILURE_RETRY(close(epoll_descriptor)) < 0)
        ERR("close");
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }

int main(int argc, char **argv)
{
    int tcp_listen_socket;
    int new_flags;
    if (argc != 3)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const int log_file = open(argv[2], O_WRONLY | O_APPEND | O_CREAT, 0777);
    if (log_file == -1)
        ERR("open");

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    tcp_listen_socket = bind_tcp_socket(atoi(argv[1]), BACKLOG);
    new_flags = fcntl(tcp_listen_socket, F_GETFL) | O_NONBLOCK;
    fcntl(tcp_listen_socket, F_SETFL, new_flags);
    doLogger(tcp_listen_socket, log_file);
    if (TEMP_FAILURE_RETRY(close(tcp_listen_socket)) < 0)
        ERR("close");
    if (close(log_file) == -1)
        ERR("close");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}
