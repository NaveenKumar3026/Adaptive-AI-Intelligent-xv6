#ifndef _PSTAT_H_
#define _PSTAT_H_

#include "types.h"

// Process classification types
enum proc_type {
  PROC_TYPE_INTERACTIVE = 0,
  PROC_TYPE_CPU_BOUND   = 1,
  PROC_TYPE_MIXED       = 2
};

// Classification threshold constants (in timer ticks)
// These define the deterministic baseline boundaries for CPU burst length
#define INTERACTIVE_BURST_THRESH 2
#define CPU_BOUND_BURST_THRESH   8

// Per-process statistics exposed to userspace
struct procinfo {
  int pid;                 // Process ID
  int ppid;                // Parent Process ID
  char name[16];           // Process name
  int state;               // Process state (enum procstate)
  uint64 cputicks;         // Total CPU ticks consumed by this process
  uint64 waitticks;        // Total ticks spent waiting in RUNNABLE state
  uint64 sched_count;      // Total times selected by the scheduler
  uint64 switches;         // Total context switches out of this process
  uint64 creation_tick;    // Clock tick when process was allocated
  uint64 last_sched_tick;  // Clock tick when process was last scheduled
  uint current_burst;      // Current continuous CPU burst (ticks)
  uint last_burst;         // Duration of previous CPU burst (ticks)
  uint est_burst;          // Exponential Moving Average (EMA) CPU burst estimate
  int proc_type;           // Classification: INTERACTIVE, CPU_BOUND, or MIXED
};

// Aggregate system workload summary for the scheduler & monitoring tools
struct workload_info {
  int num_runnable;        // Count of processes currently RUNNABLE
  int num_running;         // Count of processes currently RUNNING
  int num_sleeping;        // Count of processes currently SLEEPING
  int num_total;           // Total active processes in system
  uint avg_est_burst;      // Average estimated burst among active processes
  int pct_interactive;     // Percentage of active processes classified as INTERACTIVE (0-100)
  int pct_cpu_bound;       // Percentage of active processes classified as CPU_BOUND (0-100)
  int pct_mixed;           // Percentage of active processes classified as MIXED (0-100)
};

#endif // _PSTAT_H_
