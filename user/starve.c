#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("starve (pid %d) spawning competing tasks to induce wait accumulation...\n", getpid());

  int nkids = 3;
  for (int k = 0; k < nkids; k++) {
    int pid = fork();
    if (pid == 0) {
      // Child: high compute to compete for cores
      volatile uint64 s = 0;
      for (uint64 i = 0; i < 200000000; i++) {
        s += i * 3;
      }
      exit(0);
    }
  }

  // Parent yields and pauses, waiting for children to finish
  for (int i = 0; i < nkids; i++) {
    wait(0);
  }

  printf("starve parent (pid %d) successfully reaped all workers.\n", getpid());
  exit(0);
}
