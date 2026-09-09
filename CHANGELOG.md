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
