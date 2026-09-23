#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int phases = 4;
  if (argc > 1) {
    phases = atoi(argv[1]);
  }

  printf("bursttest (pid %d) starting dynamic burst transition test...\n", getpid());

  volatile uint64 sum = 0;
  for (int p = 0; p < phases; p++) {
    int work_len = (p % 2 == 0) ? 5000000 : 80000000;
    int sleep_len = (p % 2 == 0) ? 6 : 1;

    printf("bursttest phase %d: work=%d, sleep=%d\n", p + 1, work_len, sleep_len);
    for (int r = 0; r < 20; r++) {
      for (int i = 0; i < work_len; i++) {
        sum += (i ^ (r + p));
      }
      pause(sleep_len);
    }
  }

  printf("bursttest (pid %d) completed (sum=%lu).\n", getpid(), sum);
  exit(0);
}
