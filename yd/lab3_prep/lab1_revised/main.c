#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))


#define SMH_SIZE 1028

typedef struct {
  sem_t sem;
  pthread_barrier_t bar;
  int data[10];
} shared_memory_t;

void print_table(int size, int* score)
{
    printf("-----------GAME SCORE------------\n");
    for(int i = 0; i < size; i++)
    {
        printf("%d. %d\n", i + 1, score[i]);
    }
}

void child_work(int rounds, int index, shared_memory_t* shm)
{
    srand(getpid());
    int failure;
    for(int j = 0; j < rounds; j++){
        failure = rand() % 100;
        if(failure <= 5){
            printf("[%d]Process failed\n", getpid());
        }
        shm->data[index] = rand() % 10 + 1;
        printf("[%d] set value to %d\n", getpid(), shm->data[index]);
        pthread_barrier_wait(&shm->bar);
        pthread_barrier_wait(&shm->bar);

        printf("[%d] I've got %d points\n", getpid(), shm->data[index]);

        pthread_barrier_wait(&shm->bar);
    }
    
    exit(EXIT_SUCCESS);
}

void parent_work(int n, shared_memory_t* shm, int rounds)
{
        int buffer;
    // sigset(SIGPIPE, SIG_IGN);

    int* score = (int*)malloc(sizeof(int) * n);
    int* playersWithMaxCard = (int*)malloc(sizeof(int) * n);
    if(score == NULL || playersWithMaxCard == NULL)
        ERR("malloc");

    int max = -1;
    int playersNum = 0;
    for(int i = 0; i < rounds; i++){
      pthread_barrier_wait(&shm->bar);
        printf("----------------- ROUND %d!----------\n", i);
        max = -1;
        playersNum = 0;
        buffer = -1;
        for(int k = 0; k < n; k++)
        {
            if (shm->data[k] > max) {
                memset(playersWithMaxCard, 0, n * sizeof(int));
                playersWithMaxCard[k] = 1;
                playersNum = 1;
                max = shm->data[k];
            } else if (shm->data[k] == max) {
                playersWithMaxCard[k] = 1;
                playersNum++;
            }
        }
        // max = -1;
        // playersNum = 0; // Number of players having the maximum number of cards
        // memset(playersWithMaxCard, 0, sizeof(int) * n);
        int count;
        for (int j = 0; j < n; j++) {
            count = shm->data[j];
            if(count < 0)
                ERR("read");
            if(count == 0)
            {
                printf("Player %d did not respond\n", j);
                continue;
            }
            if(buffer > max){
                max = buffer;
                memset(playersWithMaxCard, 0, sizeof(int) * n);
                playersNum = 0;
            }
            playersNum++;
            playersWithMaxCard[j] = 1;
        }
        printf("Read flag!\n");
        for(int k = 0; k < n; k++)
        {
            if(playersWithMaxCard[k])
                score[k] += (int)(rounds / playersNum);
        }
        printf("Score flag!\n");
        pthread_barrier_wait(&shm->bar);
        pthread_barrier_wait(&shm->bar);
        
    }

    while (wait(NULL) > 0)
      wait(NULL);
    print_table(n, score);
    free(score);
    exit(EXIT_SUCCESS);
}

void create_children(int n, shared_memory_t* shm, int rounds)
{
    pid_t pid;
    for (int i = 0; i < n; i++) {
        pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // Child process
            child_work(rounds, i, shm);
        }   
    }
    parent_work(n, shm, rounds);
}

int main(int argc, char** argv) {
    if(argc != 3)
        ERR("wrong number of arguments");

    int m = atoi(argv[2]);
    int n = atoi(argv[1]);
    if(m < 5 || n < 2 || n > 5 || m > 10)
        ERR("Invalid arguments");
    int shm_fd;
    if ((shm_fd = open("./shared.txt", O_CREAT | O_RDWR | O_TRUNC, -1)) == -1) 
      ERR("open semophore");
    if (ftruncate(shm_fd, SMH_SIZE) == -1) 
      ERR("ftruncate");
    
    shared_memory_t* shm;
    if ((shm = (shared_memory_t*)mmap(NULL, SMH_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
      ERR("mmap failed");
    
    pthread_barrierattr_t attr;
    pthread_barrierattr_init(&attr);
    pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(&shm->bar, &attr, n + 1);

    if (sem_init(&shm->sem, 1, 0) == -1) 
      ERR("sem_init");
    
    create_children(n, shm, m);

    sem_destroy(&shm->sem);
    pthread_barrier_destroy(&shm->bar);
    pthread_barrierattr_destroy(&attr);
    if (munmap(shm, SMH_SIZE))
      ERR("munmap");

    
    return 0;
}

