#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void print_table(int size, int* score)
{
    printf("-----------GAME SCORE------------\n");
    for(int i = 0; i < size; i++)
    {
        printf("%d. %d\n", i + 1, score[i]);
    }
}

void child_work(int rounds, int* pipe1, int* pipe2)
{
    srand(getpid());
    int* cardsUsed = (int*) malloc(sizeof(int) * rounds);
    if(cardsUsed == NULL)
        ERR("malloc");
    memset(cardsUsed, 0, sizeof(int) * rounds);
    if(close(pipe1[1]) || close(pipe2[0]))
        ERR("close pipe");

    int buffer;
    int failure;
    for(int j = 0; j < rounds; j++){
            // Read message from parent
        failure = rand() % 100;
        if(failure <= 5){
            printf("[%d]Process failed\n", getpid());
            break;
        }
        do
        {
            buffer = rand() % rounds;
        }while(cardsUsed[buffer] != 0);
        cardsUsed[buffer] = 1;
        buffer += 1;
        if(write(pipe2[1], &buffer, sizeof(int)) < 0)
            ERR("write");
        if(read(pipe1[0], &buffer, sizeof(int)) < 0)
            ERR("read");
        //printf("[%d]New round has started! Received from parent %d\n", getpid() ,buffer);        
            
    }
    if(close(pipe1[0]) || close(pipe2[1]))
        ERR("close pipe");
    free(cardsUsed);
    exit(EXIT_SUCCESS);
}

void parent_work(int n, int** pipe1, int** pipe2, int rounds)
{
    int buffer;
    sigset(SIGPIPE, SIG_IGN);
    for(int i = 0; i < n; i++)
    {
        if(close(pipe1[i][0]) || close(pipe2[i][1]))
            ERR("close pipe");
    }
    int* score = (int*)malloc(sizeof(int) * n);
    int* playersWithMaxCard = (int*)malloc(sizeof(int) * n);
    if(score == NULL)
        ERR("malloc");
    if(playersWithMaxCard == NULL)
        ERR("malloc");
    int max;
    int playersNum;
    for(int i = 0; i < rounds; i++){
        puts("New round!");
        buffer = -1;
        for(int k = 0; k < n; k++)
        {
            if(write(pipe1[k][1], &buffer, sizeof(int)) < 0){
                if(errno == EPIPE){
                    printf("Player %d has disconnected\n", k);
                }
                else
                    ERR("write");
            }
        }
        max = -1;
        playersNum = 0; // Number of players having the maximum number of cards
        memset(playersWithMaxCard, 0, sizeof(int) * n);
        int count;
        for (int j = 0; j < n; j++) {
            count = read(pipe2[j][0], &buffer, sizeof(int));
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
        
    }
    for(int i = 0; i < n; i++)
    {
        if(close(pipe1[i][1]) || close(pipe2[i][0]))
            ERR("close pipe");
    }
    wait(NULL);
    print_table(n, score);
    free(score);
    exit(EXIT_SUCCESS);
}

void create_children(int n, int** pipe1, int** pipe2, int rounds)
{
    pid_t pid;
    for (int i = 0; i < n; i++) {
        pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // Child process
            for(int j = 0; j < n; j++){
                if(j != i)
                {
                    if(close(pipe1[j][0]) || close(pipe1[j][1]) || close(pipe2[j][0]) || close(pipe2[j][1]))
                        ERR("close");
                }
            }
            child_work(rounds, pipe1[i], pipe2[i]);
        }   
    }
    parent_work(n, pipe1, pipe2, rounds);
}

int main(int argc, char** argv) {
    int i;
    pid_t pid;
    char buffer[256];
    if(argc != 3)
        ERR("wrong number of arguments");

    int m = atoi(argv[2]);
    int n = atoi(argv[1]);
    if(m < 5 || n < 2 || n > 5 || m > 10)
        ERR("Invalid arguments");
    
    int** pipe1 = (int**)malloc(sizeof(int*) * n);
    int** pipe2 = (int**) malloc(sizeof(int*) * n);
    if(pipe1 == NULL || pipe2 == NULL)
        ERR("malloc");
    for(int i = 0; i < n; i++)
    {
        pipe1[i] = (int*) malloc(sizeof(int) * 2);
        pipe2[i] = (int*) malloc(sizeof(int) * 2);
        if(pipe1[i] == NULL || pipe2[i] == NULL)
            ERR("malloc");
    }
    // Create pipes
    for (i = 0; i < n; i++) {
        if (pipe(pipe1[i]) == -1 || pipe(pipe2[i]) == -1)
            ERR("pipe");
    }
    create_children(n, pipe1, pipe2, m);
    // Create child processes
    free(pipe1);
    free(pipe2);

    // Parent process
    

    return 0;
}

