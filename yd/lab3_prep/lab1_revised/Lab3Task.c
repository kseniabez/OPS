#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>

#define ERR(source) \
        (fprintf(stderr,"%s:%d", __FILE__,__LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))
#define SHM_SIZE 1024
typedef struct
{
        sem_t sem;
        pthread_barrier_t bar;
        int data[10];
}SharedMemory;
void usage(char* name)
{
        fprintf(stderr, "USAGE: %s N M\n", name);
        exit(EXIT_FAILURE);
}
void children_work(int id, SharedMemory* shm, int rounds)
{
        srand(getpid());
        for(int i = 0; i < rounds; i++)
        {
                shm->data[id] = rand() % 10 + 1;
                printf("[%d]Set value to %d\n",getpid() ,shm->data[id]);
                pthread_barrier_wait(&shm->bar); // Wait for all players to give values
                       
                pthread_barrier_wait(&shm->bar); // Wait for main process to assign points
                printf("[%d] I've got %d points\n", getpid(), shm->data[id]);
                
                pthread_barrier_wait(&shm->bar); // Wait for all players to print points before starting new round
        }
        exit(EXIT_SUCCESS);
}
void create_children(int num, SharedMemory* shm, int rounds)
{
        pid_t pid;
        for(int i = 0; i < num; i++)
        {
                pid = fork();
                if(pid < 0)
                {
                    ERR("fork");
                }
                if(pid == 0)
                {
                        children_work(i, shm, rounds);
                        exit(EXIT_SUCCESS);
                }
        }
}
int main(int argc, char** argv)
{
        int n, m;
        if(argc < 3)
                usage(argv[0]);
        n = atoi(argv[1]);
        m = atoi(argv[2]);
        if(n < 2 || n > 5 || m < 5 || m > 10)
                usage(argv[0]);
        int shm_fd;
        if((shm_fd = open("./shared.txt", O_CREAT | O_RDWR | O_TRUNC, -1)) == -1)
                ERR("open");
        if(ftruncate(shm_fd, SHM_SIZE) == -1)
                ERR("ftruncate");
        SharedMemory* shm;
        if((shm = (SharedMemory*)mmap(NULL, SHM_SIZE,PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
                ERR("mmap");

        pthread_barrierattr_t attr;
        pthread_barrierattr_init(&attr);
        pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_barrier_init(&shm->bar,&attr, n + 1);

        if(sem_init(&shm->sem, 1, 0) == -1)
                ERR("sem_init");
        create_children(n, shm, m);
        int* max = (int*)malloc(sizeof(int) * n);
        int* score = (int*) malloc(sizeof(int) * n);
        if(max == NULL || score == NULL)
                ERR("malloc");
        memset(max, 0, n * sizeof(int));
        memset(score, 0, n * sizeof(int));
        int numOfMax = 0;
        int maxVal = -1;
        for(int i = 0; i < m; i++)
        {
                pthread_barrier_wait(&shm->bar); // Wait till all players give values
                for(int j = 0; j < n; j++)
                {
                        printf("player %d has value %d\n", j, shm->data[j]);
                        //printf("Max value now %d\n", maxVal);
                        if(shm->data[j] > maxVal)
                        {
                                memset(max, 0, n * sizeof(int));
                                max[j] = 1;
                                numOfMax = 1;
                                maxVal = shm->data[j];

                        } 
                        else if(shm->data[j] == maxVal)
                        {
                                //printf("There's %d players with maxVal\n", numOfMax);
                                max[j] = 1;
                                numOfMax++;
                        }
                }
                //printf("Players with max number %d\n", numOfMax);
                for(int j = 0; j < n; j++)
                {
                        if(shm->data[j] == maxVal)
                        {
                                score[j] += (int)(m / numOfMax);
                                shm->data[j] = (int)(m / numOfMax);
                        }
                        else
                                shm->data[j] = 0;
                }
                printf("Player(s) ");
                for(int j = 0; j < n; j++)
                {
                        if(max[j])
                                printf("%d ", j);
                }
                printf("got max score - %d\n", maxVal);
                maxVal = -1;
                numOfMax = 0;
                memset(max, 0, n * sizeof(int));
                pthread_barrier_wait(&shm->bar); // Sync with players letting them consume points
                
                pthread_barrier_wait(&shm->bar); // Wait for all players to print points before starting new round
        }
        for(int i = 0; i < n; i++)
        {
                printf("[%d] %d points\n", i, score[i]);
        }
        sem_destroy(&shm->sem);
        pthread_barrier_destroy(&shm->bar);
        pthread_barrierattr_destroy(&attr);
        if(munmap(shm, SHM_SIZE))
                ERR("munmap");
        free(max);
        free(score);
        while(wait(NULL) > 0)
                ;
        return EXIT_SUCCESS;

}
