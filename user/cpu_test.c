#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  uint64 limit = 0; // 0 for continuous CPU computation
  if (argc > 1) {
    limit = atoi(argv[1]);
  }

  printf("cpu_test (pid %d) starting intense CPU-bound work...\n", getpid());

  volatile uint64 sum = 0;
  for (uint64 i = 0; limit == 0 || i < limit; i++) {
    sum += (i * 3 + 7) ^ (i >> 2);
  }

  printf("cpu_test (pid %d) finished calculation (checksum %lu)\n", getpid(), sum);
  exit(0);
}
