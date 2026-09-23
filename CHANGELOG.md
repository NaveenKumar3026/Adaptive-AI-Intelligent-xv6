# Changelog - Adaptive Intelligent xv6 (Phase 1)

All modifications and additions made to xv6-riscv for **Phase 1: Intelligent Process Monitoring and Baseline Infrastructure** are recorded below.

---

## Added Files

### 1. `kernel/pstat.h`
- Defined `enum proc_type` (`PROC_TYPE_INTERACTIVE`, `PROC_TYPE_CPU_BOUND`, `PROC_TYPE_MIXED`).
- Defined threshold constants `INTERACTIVE_BURST_THRESH` (2) and `CPU_BOUND_BURST_THRESH` (8).
- Defined `struct procinfo` for exporting per-process metrics safely across user-space boundary.
- Defined `struct workload_info` for exporting system-wide workload aggregates.

### 2. `user/monitor.c`
- Created user-space monitoring dashboard displaying formatted process table and workload breakdown.
- Added support for one-shot snapshot and repeating monitor modes (`monitor [iterations] [interval]`).
- Placed process information table buffer in `.bss` (static) to prevent user stack page overflow.

### 3. `user/cpu_test.c`
- Implemented intensive CPU arithmetic workload to validate CPU tick accounting and `CPU_BOUND` classification.

### 4. `user/sleep_test.c`
- Implemented periodic sleep workload (`pause()`) to validate waiting-time accumulation and `INTERACTIVE` classification.

### 5. `user/mixed_test.c`
- Implemented interleaved compute bursts and sleep intervals to validate `MIXED` classification.

### 6. `test_phase1.py`
- Implemented automated Python-based test runner that boots xv6 in QEMU, launches concurrent background workloads, and verifies monitoring outputs.

### 7. `PHASE1.md`
- Added comprehensive Phase 1 architecture and technical reference documentation.

### 8. `CHANGELOG.md`
- Created this file documenting changes.

---

## Modified Files

### 1. `kernel/proc.h`
- Included `pstat.h`.
- Added monitoring and accounting fields to `struct proc`:
  - `uint64 cputicks`: Cumulative CPU ticks.
  - `uint64 waitticks`: Cumulative waiting ticks in `RUNNABLE` state.
  - `uint64 sched_count`: Scheduler selection count.
  - `uint64 switches`: Context switch count.
  - `uint64 creation_tick`: Process creation timestamp.
  - `uint64 last_sched_tick`: Timestamp of last dispatch.
  - `uint current_burst`: Running tick count for active burst.
  - `uint last_burst`: Previous completed CPU burst duration.
  - `uint est_burst`: Exponential Moving Average (EMA) burst length estimate.
  - `int proc_type`: Process classification.

### 2. `kernel/proc.c`
- Updated `allocproc()` to initialize all monitoring fields.
- Updated `freeproc()` to reset all monitoring fields.
- Updated `scheduler()` to increment `p->sched_count` and record `p->last_sched_tick`.
- Updated `sched()` to increment `p->switches`, compute CPU burst lengths on sleep/exit, calculate integer EMA estimates, and evaluate deterministic classification.
- Added `clock_tick_accounting()` to scan the process table on each clock tick and increment `waitticks` for `RUNNABLE` processes.
- Added `get_proc_stats()` to safely copy process metrics to user space via `copyout()`.
- Added `get_workload_stats()` to compute system-wide workload distributions and averages.

### 3. `kernel/trap.c`
- Updated `usertrap()` to record CPU ticks and current burst progress on timer interrupts for active user processes.
- Updated `kerneltrap()` to record CPU ticks on timer interrupts for active kernel processes.
- Updated `clockintr()` on CPU 0 to trigger `clock_tick_accounting()`.

### 4. `kernel/syscall.h`
- Assigned syscall number `SYS_getprocinfo` (23).
- Assigned syscall number `SYS_getworkload` (24).

### 5. `kernel/syscall.c`
- Added external declarations for `sys_getprocinfo` and `sys_getworkload`.
- Registered `sys_getprocinfo` and `sys_getworkload` in `syscalls[]` table.

### 6. `kernel/sysproc.c`
- Implemented `sys_getprocinfo()` to handle user arguments and stream process stats to user memory.
- Implemented `sys_getworkload()` to copy aggregate workload statistics to user memory.

### 7. `kernel/defs.h`
- Added forward declarations for `struct procinfo`, `struct workload_info`, `get_proc_stats()`, `get_workload_stats()`, and `clock_tick_accounting()`.

### 8. `user/user.h`
- Included `kernel/pstat.h`.
- Declared user-space functions `getprocinfo()` and `getworkload()`.

### 9. `user/usys.pl`
- Added entry stubs for `getprocinfo` and `getworkload`.

### 10. `Makefile`
- Added `$U/_monitor`, `$U/_cpu_test`, `$U/_sleep_test`, and `$U/_mixed_test` to `UPROGS`.

---

# Changelog - Adaptive Intelligent xv6 (Phase 2)

All modifications and additions made to xv6-riscv for **Phase 2: Adaptive Dynamic Priority Scheduling and Dynamic Time Slicing** are recorded below.

---

## Added Files

### 1. `PHASE2_DESIGN.md`
- Comprehensive architecture specification and design document for Phase 2 adaptive scheduler.

### 2. `test_phase2.py`
- Automated test script validating dynamic priority scoring, starvation-free aging, and multi-tick time slice enforcement in QEMU.

---

## Modified Files

### 1. `kernel/pstat.h`
- Added priority scoring constants (`PRIO_BASE_INTERACTIVE`, `PRIO_BASE_MIXED`, `PRIO_BASE_CPU_BOUND`, `AGING_WEIGHT`, `BURST_PENALTY_WEIGHT`, `MAX_PRIORITY`, `MIN_PRIORITY`).
- Added `priority`, `time_slice`, and `ticks_in_slice` to `struct procinfo`.

### 2. `kernel/proc.h`
- Added `priority`, `time_slice`, and `ticks_in_slice` to `struct proc`.

### 3. `kernel/defs.h`
- Added prototypes for `calc_dynamic_priority()` and `calc_time_slice()`.

### 4. `kernel/proc.c`
- Implemented `calc_dynamic_priority(struct proc *p)`: Combines base type score, waiting-time aging bonus, and burst penalty.
- Implemented `calc_time_slice(struct proc *p)`: Dynamic quantum allocation (1 tick for Interactive, 2 ticks for Mixed, 4 ticks for CPU-bound).
- Updated `allocproc()` and `freeproc()` to initialize and reset Phase 2 fields.
- Re-architected `scheduler()`: Two-pass dynamic priority selection over `proc[NPROC]` with deadlock-free single-lock acquisition and wait-time reset on dispatch.
- Updated `get_proc_stats()` to stream Phase 2 priority and time-slice metrics to user space.

### 5. `kernel/trap.c`
- Updated `usertrap()` and `kerneltrap()`: Increment `ticks_in_slice` and enforce multi-tick time slice budgets before triggering `yield()`.

### 6. `user/monitor.c`
- Expanded process dashboard table to display dynamic priority (`PRIO`) and time slice allocation (`SLICE`) columns.

---

# Changelog - Adaptive Intelligent xv6 (Phase 3)

All modifications and additions made to xv6-riscv for **Phase 3: Intelligent Prediction, Anomaly Detection & Safe Self-Healing** are recorded below.

---

## Added Files

### 1. `user/stable.c`
- Predictable workload running constant computation and sleep cycles to validate low prediction error.

### 2. `user/bursttest.c`
- Multi-phase workload alternating between micro and heavy compute bursts to validate dynamic trend forecasting.

### 3. `user/anomaly.c`
- Workload generating erratic burst surges and rapid state transitions to validate anomaly detection and self-healing damping.

### 4. `user/starve.c`
- Workload creating high CPU competition to validate real-time starvation detection and automatic priority boost healing.

### 5. `test_phase3.py`
- End-to-end Python test runner validating Phase 3 prediction accuracy, anomaly scores, health status, and self-healing interventions.

### 6. `PHASE3_DESIGN.md`
- Comprehensive architecture specification for Phase 3 prediction, anomaly detection, and self-healing.

### 7. `PHASE3.md`
- Full reference documentation for Phase 3 algorithms and interfaces.

---

## Modified Files

### 1. `kernel/pstat.h`
- Added Phase 3 anomaly constants (`STARVATION_THRESH_TICKS`, `BURST_SURGE_THRESH`, `MAX_ANOMALY_SCORE`).
- Added anomaly reason bitmask flags (`ANOMALY_BURST_SURGE`, `ANOMALY_STARVATION`, `ANOMALY_THRASHING`, `ANOMALY_INSTABILITY`).
- Added `enum health_status` (`HEALTH_NORMAL`, `HEALTH_WARNING`, `HEALTH_CRITICAL`).
- Extended `struct procinfo` with: `predicted_burst`, `prediction_error`, `anomaly_score`, `anomaly_flags`, `health_status`, `healing_actions`, `last_heal_tick`.
- Extended `struct workload_info` with: `num_healthy`, `num_warning`, `num_critical`, `total_healing_actions`.

### 2. `kernel/proc.h`
- Extended `struct proc` with: `predicted_burst`, `prediction_error`, `burst_trend`, `anomaly_score`, `anomaly_flags`, `health_status`, `healing_actions`, `last_heal_tick`.

### 3. `kernel/defs.h`
- Added declarations for `update_prediction_and_health()` and `apply_self_healing()`.

### 4. `kernel/proc.c`
- Initialized and reset Phase 3 fields in `allocproc()` and `freeproc()`.
- Implemented `update_prediction_and_health(p)`: Computes prediction error, updates trend-aware double EMA, detects anomalies, evaluates health status, and triggers self-healing.
- Implemented `apply_self_healing(p)`: Non-destructive corrective interventions (priority boost for starvation, trend reset for surge/instability, slice expansion for thrashing).
- Integrated `update_prediction_and_health()` into `sched()`.
- Added real-time starvation detection and automatic healing to `clock_tick_accounting()`.
- Updated `get_proc_stats()` and `get_workload_stats()` to stream full Phase 3 telemetry to user space.

### 5. `user/monitor.c`
- Redesigned monitoring dashboard to display `PRED`, `ERR`, `PRIO`, `ANOM`, `HEALS`, `HEALTH`, and a System Health Summary.

### 6. `Makefile`
- Added `$U/_stable`, `$U/_bursttest`, `$U/_anomaly`, and `$U/_starve` to `UPROGS`.


