#define _GNU_SOURCE
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MAX_QUEUE_NAME 50
#define MAX_MSG_SIZE 100

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), exit(EXIT_FAILURE))

volatile sig_atomic_t running = 1;
int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigint_handler(int sig)
{
    running = 0;
}
void usage(void)
{
	fprintf(stderr, "USAGE: server_queue_name\n");
	exit(EXIT_FAILURE);
}
void client_work(mqd_t mq, mqd_t mq_s)
{
    
    int a, b;
    while(1)
    {
        printf("Enter two integers: ");
        if (scanf("%d%d", &a, &b) == EOF) break;
    
        char msg[MAX_MSG_SIZE]; 
        sprintf(msg, "%d,%d,%d", getpid(), a, b);
        if (TEMP_FAILURE_RETRY(mq_send(mq_s, msg, strlen(msg) + 1, 0)))
            ERR("mq_send");
        nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 100000000}, NULL); 
        if (mq_receive(mq, msg, sizeof(msg), 0) == -1)
        {
            printf("Client [%d] reply hasn't arrived within 100ms\n", getpid()); 
            break;
        }    
        printf("Result: %s\n", msg);
    }
}
int main(int argc, char** argv)
{
	if(argc !=2)
	{
		usage();
	}
    //sethandler(sigint_handler, SIGINT);
    pid_t pid = getpid();
    char pid_str[20];
    sprintf(pid_str, "%d", pid);

    char queue[MAX_QUEUE_NAME];
    snprintf(queue, MAX_QUEUE_NAME, "/%s", pid_str);
    
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_MSG_SIZE;
	mqd_t mq, mq_s;
    if((mq = TEMP_FAILURE_RETRY(mq_open(queue, O_RDWR | O_CREAT | O_NONBLOCK, 0600, &attr))) == (mqd_t)-1)
	    ERR("mq open");
    if((mq_s = TEMP_FAILURE_RETRY(mq_open(argv[1], O_RDWR, 0600, &attr))) == (mqd_t)-1)
	    ERR("mq_s open");

    printf("client[%d] Opened message queues:\n", getpid());
    printf("%s\n%s\n", queue, argv[1]);
    client_work(mq, mq_s);

    if (mq_close(mq_s) == -1)
        ERR("mq_s close");

    if (mq_close(mq) == -1)
        ERR("mq_s close");
    if (mq_unlink(queue))
        ERR("mq_unlink");

    return EXIT_SUCCESS;
}
