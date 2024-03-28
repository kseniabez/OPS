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
void server_work(mqd_t mq_s, mqd_t mq_d, mqd_t mq_m)
{
    char msg[MAX_MSG_SIZE];
    int client_pid, a, b;
    char queue_c[MAX_QUEUE_NAME];
    int message=0;
    while (running) 
    {
        //nanosleep(&(struct timespec){.tv_sec = 10, .tv_nsec = 0}, NULL); 
        if (mq_receive(mq_s, msg, sizeof(msg), NULL) != -1) {
            printf("Received message from mq_s: %s\n", msg);
            sscanf(msg, "%d,%d,%d", &client_pid, &a, &b);
            sprintf(msg, "%d", a+b);
            message=1;
        }
        if (message==0 && mq_receive(mq_d, msg, sizeof(msg), NULL) != -1) {
            printf("Received message from mq_s: %s\n", msg);
            sscanf(msg, "%d,%d,%d", &client_pid, &a, &b);
            if(b==0) sprintf(msg, "Error :(");
            else sprintf(msg, "%d", a/b);
            message=1;
        }
        if (message==0 && mq_receive(mq_m, msg, sizeof(msg), NULL) != -1) {
            printf("Received message from mq_s: %s\n", msg);
            sscanf(msg, "%d,%d,%d", &client_pid, &a, &b);
            if(b==0) sprintf(msg, "Error :(");
            else sprintf(msg, "%d", a%b);
            message=1;
        }
        if(message==1)
        {
            printf("Sending result to %d: %s\n", client_pid, msg);
            sprintf(queue_c, "/%d", client_pid);
            struct mq_attr attr;
            attr.mq_maxmsg = 10;
            attr.mq_msgsize = MAX_MSG_SIZE;
            mqd_t mq_c;
            if((mq_c = TEMP_FAILURE_RETRY(mq_open(queue_c, O_RDWR, 0600, &attr))) == (mqd_t)-1)
            {
                printf("Server: Client %d has exited. Failed to send result\n", client_pid);
                message=0;
                continue;
            }
	            
            if (TEMP_FAILURE_RETRY(mq_send(mq_c, msg, strlen(msg) + 1, 0)))
                ERR("mq_send");
            if (mq_close(mq_c) == -1)
                ERR("mq_m close");
        }
        message=0;
    }

    

}

int main()
{
    sethandler(sigint_handler, SIGINT);
    pid_t pid = getpid();
    char pid_str[20];
    sprintf(pid_str, "%d", pid);

    char queue_s[MAX_QUEUE_NAME], queue_d[MAX_QUEUE_NAME], queue_m[MAX_QUEUE_NAME];
    snprintf(queue_s, MAX_QUEUE_NAME, "/%s_s", pid_str);
    snprintf(queue_d, MAX_QUEUE_NAME, "/%s_d", pid_str);
    snprintf(queue_m, MAX_QUEUE_NAME, "/%s_m", pid_str);
    //printf("%s\n%s\n%s\n", queue_s, queue_d, queue_m);
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_MSG_SIZE;
	mqd_t mq_s, mq_d, mq_m;
    if((mq_s = TEMP_FAILURE_RETRY(mq_open(queue_s, O_RDWR | O_CREAT | O_NONBLOCK, 0600, &attr))) == (mqd_t)-1)
	ERR("mq_s open");
    if((mq_d = TEMP_FAILURE_RETRY(mq_open(queue_d, O_RDWR | O_CREAT | O_NONBLOCK, 0600, &attr))) == (mqd_t)-1)
	ERR("mq_d open");
    if((mq_m = TEMP_FAILURE_RETRY(mq_open(queue_m, O_RDWR | O_CREAT | O_NONBLOCK, 0600, &attr))) == (mqd_t)-1)
	ERR("mq_m open");

    printf("Created message queues:\n");
    printf("PID_s: %s\nPID_d: %s\nPID_m: %s\n", queue_s, queue_d, queue_m);

	//nanosleep(&(struct timespec){.tv_sec = 10, .tv_nsec = 0}, NULL);    
    server_work(mq_s, mq_d, mq_m);

    // Cleanup message queues
    if (mq_close(mq_s) == -1 || mq_close(mq_d) == -1 || mq_close(mq_m) == -1)
        ERR("mq_close");

    if (mq_unlink(queue_s) == -1 || mq_unlink(queue_d) == -1 || mq_unlink(queue_m) == -1)
        ERR("mq_unlink");

    return EXIT_SUCCESS;
}
