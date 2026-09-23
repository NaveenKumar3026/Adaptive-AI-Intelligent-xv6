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

// Phase 2: Priority and Aging Constants
#define PRIO_BASE_INTERACTIVE   50
#define PRIO_BASE_MIXED         30
#define PRIO_BASE_CPU_BOUND     10
#define AGING_WEIGHT            2
#define BURST_PENALTY_WEIGHT    1
#define MAX_PRIORITY            100
#define MIN_PRIORITY            1

// Phase 3: Prediction, Anomaly Detection & Self-Healing Constants
#define STARVATION_THRESH_TICKS 20   // Waitticks before flagging starvation anomaly
#define BURST_SURGE_THRESH      4    // Burst jump factor threshold
#define MAX_ANOMALY_SCORE       100

// Anomaly reason bitmask flags
#define ANOMALY_NONE             0x00
#define ANOMALY_BURST_SURGE      0x01 // Sudden large burst increase
#define ANOMALY_STARVATION       0x02 // Process waiting too long in RUNNABLE
#define ANOMALY_THRASHING        0x04 // Rapid oscillation / unstable burst pattern
#define ANOMALY_INSTABILITY      0x08 // High sustained prediction error

// Process Health Status
enum health_status {
  HEALTH_NORMAL   = 0,
  HEALTH_WARNING  = 1,
  HEALTH_CRITICAL = 2
};

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
  int priority;            // Dynamic priority score (Phase 2)
  uint time_slice;         // Allocated execution time slice (ticks, Phase 2)
  uint ticks_in_slice;     // Ticks elapsed in current time slice (Phase 2)
  
  // Phase 3: Intelligent Prediction & Self-Healing Metrics
  uint predicted_burst;    // Predicted next CPU burst length (ticks)
  uint prediction_error;   // Absolute error |actual - predicted| of previous burst
  int anomaly_score;       // Composite anomaly severity score (0 - 100)
  int anomaly_flags;       // Bitmask of active anomaly flags
  int health_status;       // Health state: HEALTH_NORMAL, HEALTH_WARNING, HEALTH_CRITICAL
  uint healing_actions;    // Cumulative count of safe self-healing actions applied
  uint64 last_heal_tick;   // Clock tick when self-healing was most recently applied
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

  // Phase 3: System Health Summary
  int num_healthy;         // Count of active processes in HEALTH_NORMAL
  int num_warning;         // Count of active processes in HEALTH_WARNING
  int num_critical;        // Count of active processes in HEALTH_CRITICAL
  int total_healing_actions;// Total self-healing actions taken across all processes
};

#endif // _PSTAT_H_
