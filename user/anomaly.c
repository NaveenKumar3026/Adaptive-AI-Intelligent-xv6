#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int rounds = 30;
  if (argc > 1) {
    rounds = atoi(argv[1]);
  }

  printf("anomaly (pid %d) generating erratic burst surges & rapid state swings...\n", getpid());

  volatile uint64 acc = 0;
  for (int r = 0; r < rounds; r++) {
    // Alternate between micro bursts and sudden massive compute surges
    int work = (r % 3 == 0) ? 120000000 : 200000;
    int sleep_ticks = (r % 2 == 0) ? 1 : 5;

    for (int i = 0; i < work; i++) {
      acc += (i * 7 + r);
    }
    pause(sleep_ticks);
  }

  printf("anomaly (pid %d) finished (acc=%lu).\n", getpid(), acc);
  exit(0);
}
