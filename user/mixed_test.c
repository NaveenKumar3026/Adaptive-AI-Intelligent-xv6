#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int rounds = 200;
  int work_loop = 80000000;
  int sleep_ticks = 4;

  if (argc > 1) {
    rounds = atoi(argv[1]);
  }

  printf("mixed_test (pid %d) starting mixed CPU/I-O workload (%d rounds)...\n",
         getpid(), rounds);

  volatile uint64 acc = 0;
  for (int r = 0; r < rounds; r++) {
    // CPU burst
    for (int i = 0; i < work_loop; i++) {
      acc += (i ^ (r + 1));
    }
    // Sleep (simulated I/O block)
    pause(sleep_ticks);
  }

  printf("mixed_test (pid %d) completed (acc %lu).\n", getpid(), acc);
  exit(0);
}
