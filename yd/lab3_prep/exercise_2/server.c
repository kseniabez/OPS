#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ERR(source) \
  (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), exit(EXIT_FAILURE))


#define SHM_SIZE 1024

void usage (char *name) {
  fprintf(stderr, "USAGE: %s n\n", name);
  fprintf(stderr, "n - number of children\n");
  exit(EXIT_FAILURE);
}

typedef struct {
  int running;
  pthread_mutex_t mutex;
  sigset_t old_mask, new_mask;
} sighangling_args_t;

void* sighandling(void* args) {
  sighangling_args_t* sargs = (sighangling_args_t*)args;
  int signo;
  if (sigwait(&sargs->new_mask, &signo)) {
    ERR("sigwait");
  }
  if (signo != SIGINT)
    ERR("unexpected signal");
  pthread_mutex_lock(&sargs->mutex);
  sargs->running = 0;
  pthread_mutex_unlock(&sargs->mutex);
  return NULL;
}

int main(void* argc, char* argv[]) {
  if (argc != 2) {
    usage(argv[0]);
  }

  const int N = atoi(argv[1]);
  if (N < 3 || N >= 100) {
    usage(argv[0]);
  }

  const pid_t pid = getpid();
  srand(pid);

  printf("PID: %d\n", pid);
  int shm_fd;
  char shm_name[32];
  sprintf(shm_name, "/%d-board", pid);
  if ((shm_fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0666)) == -1) {
    ERR("shm_open");
  }
  if (ftruncate(shm_fd, SHM_SIZE) == -1) {
    ERR("ftruncate");
  }
  
  char* shm_ptr;
  if ((shm_ptr = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
    ERR("mmap");
  }

  pthread_mutex_t* mutex = (pthread_mutex_t*)shm_ptr;
  char* N_shared = shm_ptr + sizeof(pthread_mutex_t);
  char* board = (shm_ptr + sizeof(pthread_mutex_t) + sizeof(char));
  N_shared[0] = N;

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      board[i * N + j] = 1 + rand() % 9;
    }
  }

  pthread_mutexattr_t mutex_attr;
  if (pthread_mutexattr_init(&mutex_attr)) {
    ERR("pthread_mutexattr_init");
  }
  if (pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED)) {
    ERR("pthread_mutexattr_setpshared");
  }
  pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
  if (pthread_mutex_init(mutex, &mutex_attr)) {
    ERR("pthread_mutex_init");
  }

  sighangling_args_t sargs = {1, PTHREAD_MUTEX_INITIALIZER};

  sigemptyset(&sargs.new_mask);
  sigaddset(&sargs.new_mask, SIGINT);
  if (pthread_sigmask(SIG_BLOCK, &sargs.new_mask, &sargs.old_mask)) {
    ERR("pthread_sigmask");
  }

  pthread_t sighandling_thread;
  if (pthread_create(&sighandling_thread, NULL, sighandling, &sargs)) {
    ERR("pthread_create");
  }

  while (1) {
    pthread_mutex_lock(&sargs.mutex);
    if (!sargs.running) {
      pthread_mutex_unlock(&sargs.mutex);
      break;
    }
    pthread_mutex_unlock(&sargs.mutex);

    int error;
    if ((error = thread_mutex_lock(mutex))) {
      if (error == EOWNERDEAD) {
        pthread_mutex_consistent(mutex);
      } else {
        ERR("pthread_mutex_lock");
      }
    }

    for (int i = 0; i < N; i++) {
      for (int j = 0; j < N; j++) {
        printf("%d ", board[i * N + j]);
      }
      printf("\n");
    }

    putchar('\n');
    pthread_mutex_unlock(mutex);
    struct timespec ts = {3, 0};
    nanosleep(&ts, &ts);

  }

  pthread_join(sighandling_thread, NULL);

  pthread_mutexattr_destroy(&mutex_attr);
  pthread_mutex_destroy(mutex);

  if (munmap(shm_ptr, SHM_SIZE) == -1) {
    ERR("munmap");
  }
  if (shm_unlink(shm_name) == -1) {
    ERR("shm_unlink");
  }

  return EXIT_SUCCESS;
}
