#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int rounds = 100;
  int work_iters = 30000000;
  int sleep_ticks = 2;

  if (argc > 1) {
    rounds = atoi(argv[1]);
  }

  printf("stable (pid %d) running %d predictable rounds...\n", getpid(), rounds);

  volatile uint64 acc = 0;
  for (int r = 0; r < rounds; r++) {
    // Constant compute burst
    for (int i = 0; i < work_iters; i++) {
      acc += (i * 3 + 1);
    }
    // Constant sleep
    pause(sleep_ticks);
  }

  printf("stable (pid %d) finished (acc=%lu).\n", getpid(), acc);
  exit(0);
}
