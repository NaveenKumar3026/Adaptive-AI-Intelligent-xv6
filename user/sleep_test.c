#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int iterations = 1000;
  int sleep_duration = 5;

  if (argc > 1) {
    iterations = atoi(argv[1]);
  }
  if (argc > 2) {
    sleep_duration = atoi(argv[2]);
  }

  printf("sleep_test (pid %d) starting interactive sleep workload (%d iterations, %d ticks each)...\n",
         getpid(), iterations, sleep_duration);

  for (int i = 0; i < iterations; i++) {
    pause(sleep_duration);
  }

  printf("sleep_test (pid %d) finished sleeping.\n", getpid());
  exit(0);
}
