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
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>

#define DEFAULT_N 5
#define DEFAULT_M 10
#define SHM_SIZE 1024
#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

typedef struct
{
        sem_t sem;
        pthread_barrier_t bar;
        int data[10];
}SharedMemory;

void ReadArguments(int argc, char **argv, int *n, int *m);
void usage(char* name);

void child_work(int n, int m, int index, SharedMemory* shm)
{
	int num;
	int* cards = (int*)malloc(m * sizeof(int));
	if(cards==NULL) ERR("malloc");
	memset(cards, 0, sizeof(int)*m);
	srand(getpid());
	int failed=0;
    for(int i = 0; i < m; i++)
    {
		if(failed==-1 || rand()%100 <=5)
		{
			shm->data[index]=-1;
			failed=-1;
		}
		else
		{
			do
			{
				num =rand() % m + 1;
			}while(cards[num-1] != 0);
			cards[num-1]=1;
        	shm->data[index] = num;
		}
        pthread_barrier_wait(&shm->bar);
		pthread_barrier_wait(&shm->bar);
    }
	free(cards);
}
int Max(int* array, int n)
{
	int max=-1;
	for(int i=0; i<n; i++)
	{
		if(array[i]>max) max=array[i];
	}
	return max;
}	
void parent_work(int n, int m, SharedMemory* shm)
{
	int* stat = (int*)malloc(m * sizeof(int));
	int* round = (int*)malloc(m * sizeof(int));
	if(stat==NULL || round== NULL) ERR("malloc");
	for(int k=0; k<m; k++)
	{
	printf("\n--------ROUND %d-------\n", k+1);
	pthread_barrier_wait(&shm->bar);
	for(int i=0; i<n; i++)
	{
		round[i]=shm->data[i];
		if(round[i]==-1)
		{
			printf("Player %d failed\n", i);
			continue;
		}
		printf("Player %d plays %d\n", i, round[i]);
	}
	int max = Max(round,n);
	printf("---Max this round:%d---\n", max);
	int count=0;
	for(int i=0; i<n;i++)
	{
		if(round[i]==max) count++;
	}
	printf("----ROUND %d RESULTS---\n", k+1);
	for(int i=0; i<n;i++)
	{
		if(round[i]==max) 
		{
			stat[i]+=n/count;
			printf("Player %d: %d\n", i, n/count);
		}
		else printf("Player %d: 0\n", i);
	}
	pthread_barrier_wait(&shm->bar);
	}
	printf("\n--------RESULTS--------\n");
	for(int i=0; i<n; i++)
		printf("Player %d: %d\n", i, stat[i]);
	free(stat);
	free(round);
}

int main(int argc, char** argv) {
	int n,m;
	ReadArguments(argc, argv, &n, &m);

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

	int child_pids[n];
	for(int i=0; i<n; i++)
	{
		child_pids[i]=fork();
		if(child_pids[i]==0)
		{
			child_work(n, m, i, shm);
			return EXIT_SUCCESS;
		}
	}
	parent_work(n,m, shm);
	for(int i=0; i<n; i++)
	{
		waitpid(child_pids[i], NULL, 0);
	}

}
void ReadArguments(int argc, char **argv, int *n, int *m)
{
	*n=DEFAULT_N;
	*m=DEFAULT_M;
	if(argc>1)
	{
		*n=atoi(argv[1]);
		if(*n < 2 || *n > 5) usage(argv[0]);
	}
	if(argc>2)
	{
		*m=atoi(argv[2]);
		if(*m < 5 || *m > 10) usage(argv[0]);
	}
}
void usage(char* name)
{
        fprintf(stderr, "%s: n[2,5] m[5,10]", name);
        exit(EXIT_SUCCESS);
}
