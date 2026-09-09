#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static const char *state_names[] = {
  [0] "UNUSED  ",
  [1] "USED    ",
  [2] "SLEEPING",
  [3] "RUNNABLE",
  [4] "RUNNING ",
  [5] "ZOMBIE  "
};

static const char *type_names[] = {
  [0] "INTERACTIVE",
  [1] "CPU_BOUND  ",
  [2] "MIXED      "
};

static void
print_padded_str(const char *s, int width)
{
  int len = strlen(s);
  printf("%s", s);
  for (int i = len; i < width; i++) {
    printf(" ");
  }
}

static void
print_padded_num(uint64 num, int width)
{
  char buf[24];
  int i = 0;
  uint64 temp = num;

  if (num == 0) {
    buf[i++] = '0';
  } else {
    while (temp > 0) {
      buf[i++] = (temp % 10) + '0';
      temp /= 10;
    }
  }

  for (int j = i; j < width; j++) {
    printf(" ");
  }

  for (int j = i - 1; j >= 0; j--) {
    printf("%c", buf[j]);
  }
}

static struct procinfo procs[64];

static void
display_monitor(void)
{
  struct workload_info winfo;

  int count = getprocinfo(64, procs);
  if (count < 0) {
    printf("monitor: getprocinfo failed\n");
    return;
  }

  if (getworkload(&winfo) < 0) {
    printf("monitor: getworkload failed\n");
    return;
  }

  printf("==============================================================================\n");
  printf("                         ADAPTIVE XV6 PROCESS MONITOR                         \n");
  printf("==============================================================================\n");
  printf("PID    PPID   NAME            STATE       CPU   WAIT  SWITCHES  BURST  TYPE       \n");
  printf("------------------------------------------------------------------------------\n");

  for (int i = 0; i < count; i++) {
    const char *st = (procs[i].state >= 0 && procs[i].state <= 5) ? state_names[procs[i].state] : "UNKNOWN ";
    const char *tp = (procs[i].proc_type >= 0 && procs[i].proc_type <= 2) ? type_names[procs[i].proc_type] : "UNKNOWN    ";

    print_padded_num(procs[i].pid, 6);
    printf(" ");
    print_padded_num(procs[i].ppid, 6);
    printf(" ");
    print_padded_str(procs[i].name, 15);
    printf(" ");
    print_padded_str(st, 11);
    printf(" ");
    print_padded_num(procs[i].cputicks, 5);
    printf(" ");
    print_padded_num(procs[i].waitticks, 5);
    printf(" ");
    print_padded_num(procs[i].switches, 9);
    printf(" ");
    print_padded_num(procs[i].est_burst, 6);
    printf(" ");
    printf("%s\n", tp);
  }

  printf("------------------------------------------------------------------------------\n");
  printf("WORKLOAD SUMMARY:\n");
  printf("  Total Active: %d | Runnable: %d | Running: %d | Sleeping: %d\n",
         winfo.num_total, winfo.num_runnable, winfo.num_running, winfo.num_sleeping);
  printf("  Avg Est Burst: %u ticks | Interactive: %d%% | CPU-Bound: %d%% | Mixed: %d%%\n",
         winfo.avg_est_burst, winfo.pct_interactive, winfo.pct_cpu_bound, winfo.pct_mixed);
  printf("==============================================================================\n\n");
}

int
main(int argc, char *argv[])
{
  int iterations = 1;
  int delay_ticks = 10;

  if (argc > 1) {
    iterations = atoi(argv[1]);
    if (iterations <= 0)
      iterations = 1;
  }
  if (argc > 2) {
    delay_ticks = atoi(argv[2]);
    if (delay_ticks <= 0)
      delay_ticks = 10;
  }

  for (int i = 0; i < iterations; i++) {
    display_monitor();
    if (i + 1 < iterations) {
      pause(delay_ticks);
    }
  }

  exit(0);
}
