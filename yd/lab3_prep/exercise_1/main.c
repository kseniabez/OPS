#define GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MONTE_CARLO_ITERATIONS 1000000
#define LOG_LEN 8

#define ERR(source) \
  (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), exit(EXIT_FAILURE))


void child_work(int n, float* out, char* log) {
  int sample = 0;
  srand(getpid());
  int iters = MONTE_CARLO_ITERATIONS;
  while (iters-- > 0) {
    double x = ((double)rand()) / RAND_MAX;
    double y = ((double)rand()) / RAND_MAX;
    if (x * x + y * y < 1) {
      sample++;
    }
  }
  out[n] = ((float)sample) / MONTE_CARLO_ITERATIONS;
  char buf[LOG_LEN + 1];

  snprintf(buf, LOG_LEN + 1, "%7.5f\n", out[n] * 4.0f);
  memcpy(log + n * LOG_LEN, buf, LOG_LEN);
}

void parent_work(int n, float* data) {
  pid_t pid;
  double sum = 0.0;
  for (;;) {
    pid = wait(NULL);
    if (pid <= 0) {
      if (errno == ECHILD) {
        break;
      }
      ERR("waitpid");
    }
  }

  for (int i = 0; i < n; i++) {
    sum += data[i];
  }
  sum = sum / n * 4.0f;
  printf("PI = %f\n", sum); 
}

void create_children(int n, float* data, char* log) {
  while (n-- > 0) {
    switch(fork()) {
      case 0: 
        child_work(n, data, log);
        exit(EXIT_SUCCESS);

      case -1:
        ERR("fork");
        exit(EXIT_FAILURE);
    }
  }
}

void usage (char* name) {
  fprintf(stderr, "USAGE: %s n\n", name);
  fprintf(stderr, "10000 >= n > 0 - number of children\n");
  exit(EXIT_FAILURE);

}

int main(int argc, char** argv) {
  int n;
  if (argc != 2) {
    usage(argv[0]);
  }

  n = atoi(argv[1]);
  //Incorrect limits in the lab 
  if (n <= 0 || n > 10000) {
    usage(argv[0]);
  }

  int log_fd;
  // Possible bug with -1 at the end
  if((log_fd = open("./log.txt", O_RDWR | O_CREAT | O_TRUNC, -1)) == -1) {
    ERR("open");
  }
  if (ftruncate(log_fd, n * LOG_LEN) == -1) {
    ERR("ftruncate");
  }

  char* log;
  if ((log = mmap(NULL, n * LOG_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, log_fd, 0)) == MAP_FAILED) {
    ERR("mmap");
  }
  if (close(log_fd) == -1) {
    ERR("close");
  }

  float* data;
  if ((data = (float*)mmap(NULL, n * sizeof(float), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
    ERR("mmap");
  }

  create_children(n, data, log);
  parent_work(n, data);

  if (munmap(data, n * sizeof(float)) == -1) {
    ERR("munmap");
  }
  if (msync(log, n * LOG_LEN, MS_SYNC) == -1) {
    ERR("msync");
  }

  if (munmap(log, n * LOG_LEN) == -1) {
    ERR("munmap");
  }

  return EXIT_SUCCESS;
}