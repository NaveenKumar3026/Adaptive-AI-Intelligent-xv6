# Adaptive Intelligent xv6 Kernel - Phase 1 Documentation

## 1. Project Objective

The **Adaptive Intelligent xv6 Kernel** project is an in-tree enhancement to the xv6 operating system kernel designed to bring behavioral awareness, workload analysis, and adaptive scheduling to the kernel.

Rather than relying on a fixed, static scheduling policy or user-space simulation, the adaptive kernel continuously observes running workloads directly inside kernel space.

**Phase 1 Focus**: Intelligent Process Monitoring and Baseline Infrastructure.
Phase 1 implements reliable in-kernel process accounting, CPU and wait time tracking, CPU burst length estimation via Exponential Moving Average (EMA), deterministic process classification, clean system-call interfaces, aggregate workload summarization, and user-space monitoring utilities (`monitor`) without modifying xv6's default round-robin scheduler behavior.

---

## 2. xv6 Version Detected

- **Operating System / Target Architecture**: `xv6-riscv` (64-bit RISC-V, RV64GC).
- **Virtual Memory Architecture**: RISC-V Sv39 multi-level page tables with user/kernel address space isolation via `trampoline.S` and `trapframe`.
- **Interrupt Controller**: RISC-V Supervisor-mode trap handling with PLIC for external device interrupts and CLINT/ACLINT timer interrupts routed via `clockintr()`.
- **Concurrency & Multiprocessing**: Multi-core SMP (default 3 CPUs), symmetric per-CPU scheduler loops, and spinlock-protected process structures (`struct proc`).

---

## 3. Files Modified & Added

### Added Files
1. `kernel/pstat.h`: Shared header containing `struct procinfo`, `struct workload_info`, classification constants, and enum definitions.
2. `user/monitor.c`: User-space utility for real-time inspection of per-process metrics and system workload summaries.
3. `user/cpu_test.c`: Test workload generating continuous CPU-bound computation.
4. `user/sleep_test.c`: Test workload generating interactive, I/O-simulated sleep cycles.
5. `user/mixed_test.c`: Test workload alternating computation bursts and sleep intervals.
6. `test_phase1.py`: Automated non-interactive test harness validating QEMU boot, process accounting, and monitoring.
7. `PHASE1.md`: Architectural specification and Phase 1 documentation.
8. `CHANGELOG.md`: Detailed change history per file.

### Modified Files
1. `kernel/proc.h`: Extended `struct proc` with statistics fields (`cputicks`, `waitticks`, `sched_count`, `switches`, `creation_tick`, `last_sched_tick`, `current_burst`, `last_burst`, `est_burst`, `proc_type`).
2. `kernel/proc.c`:
   - Initialized metric fields in `allocproc()` and cleared them in `freeproc()`.
   - Instrumented `scheduler()` to record schedule count and timestamp.
   - Instrumented `sched()` to track context switches, calculate CPU burst transitions, update EMA burst estimates, and evaluate process classification.
   - Added `clock_tick_accounting()` to increment waiting ticks for `RUNNABLE` processes.
   - Added `get_proc_stats()` and `get_workload_stats()` to safely extract process and workload information.
3. `kernel/trap.c`:
   - Updated `usertrap()` and `kerneltrap()` to credit CPU ticks and current burst progress to the active process on timer interrupts.
   - Updated `clockintr()` on CPU 0 to invoke `clock_tick_accounting()`.
4. `kernel/syscall.h`: Added syscall numbers `SYS_getprocinfo` (23) and `SYS_getworkload` (24).
5. `kernel/syscall.c`: Registered `sys_getprocinfo` and `sys_getworkload` in the `syscalls[]` jump table.
6. `kernel/sysproc.c`: Implemented syscall handlers `sys_getprocinfo()` and `sys_getworkload()` using kernel-to-user memory copy (`copyout`).
7. `kernel/defs.h`: Added forward declarations for `struct procinfo`, `struct workload_info`, `get_proc_stats()`, `get_workload_stats()`, and `clock_tick_accounting()`.
8. `user/user.h`: Included `kernel/pstat.h` and declared `getprocinfo()` and `getworkload()` system call wrappers.
9. `user/usys.pl`: Added entry points for `getprocinfo` and `getworkload` assembly stubs.
10. `Makefile`: Included `_monitor`, `_cpu_test`, `_sleep_test`, and `_mixed_test` in `UPROGS`.

---

## 4. Architecture

```
+-----------------------------------------------------------------------------+
|                                USER SPACE                                   |
|                                                                             |
|  +-----------------+  +-------------------+  +---------------------------+  |
|  |  User Programs  |  | monitor (Utility) |  | Test Workloads (cpu/sleep)|  |
|  +--------+--------+  +---------+---------+  +-------------+-------------+  |
+-----------|---------------------|--------------------------|----------------+
            |                     | (getprocinfo / getworkload)
            v                     v                          v
+-----------------------------------------------------------------------------+
|                                KERNEL SPACE                                 |
|                                                                             |
|  +-----------------------------------------------------------------------+  |
|  |                           System Call Layer                           |  |
|  |                 sys_getprocinfo()  |  sys_getworkload()                |  |
|  +-----------------------------------+-----------------------------------+  |
|                                      |                                      |
|  +-----------------------------------v-----------------------------------+  |
|  |                 Process Monitoring & Accounting Module                |  |
|  |  * CPU Tick Accounting (usertrap / kerneltrap / clockintr)             |  |
|  |  * Wait Time Accumulator (RUNNABLE queue scanner)                     |  |
|  |  * Context Switch & Schedule Counters                                 |  |
|  |  * CPU Burst Tracker & Exponential Moving Average (EMA) Estimator     |  |
|  +-----------------------------------+-----------------------------------+  |
|                                      |                                      |
|  +-----------------------------------v-----------------------------------+  |
|  |                     Deterministic Workload Analyzer                   |  |
|  |  * Classification Engine: INTERACTIVE vs CPU_BOUND vs MIXED           |  |
|  |  * System-wide State Aggregator: Runnable / Sleeping / Type breakdown |  |
|  +-----------------------------------+-----------------------------------+  |
|                                      |                                      |
|  +-----------------------------------v-----------------------------------+  |
|  |                      xv6 Baseline Scheduler                           |  |
|  |                 (Round-Robin on proc[NPROC] table)                     |  |
|  |                                                                       |  |
|  |      [Ready for Phase 2: Dynamic Policy Selection / Multi-Queue]      |  |
|  +-----------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------+
```

---

## 5. Process Statistics Definitions

Each process in xv6 is monitored via the following structural fields:

| Field | Type | Description |
| :--- | :--- | :--- |
| `cputicks` | `uint64` | Total timer interrupt ticks during which this process was executing on a CPU core. |
| `waitticks` | `uint64` | Total timer ticks during which the process was in `RUNNABLE` state waiting for CPU. |
| `sched_count` | `uint64` | Total number of times the scheduler selected this process and transferred execution to it. |
| `switches` | `uint64` | Total number of context switches transferring control out of this process via `sched()`. |
| `creation_tick` | `uint64` | System clock `ticks` timestamp when `allocproc()` created the process. |
| `last_sched_tick` | `uint64` | System clock `ticks` timestamp when the process was most recently dispatched. |
| `current_burst` | `uint` | Number of ticks elapsed in the current continuous CPU execution burst. |
| `last_burst` | `uint` | Duration in ticks of the process's most recently completed CPU burst. |
| `est_burst` | `uint` | Exponential Moving Average (EMA) estimate of the process's CPU burst length. |
| `proc_type` | `int` | Behavioral classification: `PROC_TYPE_INTERACTIVE`, `PROC_TYPE_CPU_BOUND`, or `PROC_TYPE_MIXED`. |

---

## 6. CPU-Time Calculation

- **Accounting Origin**: In xv6-riscv, hardware timer interrupts occur periodically (~10 Hz). When a timer interrupt is received (`scause == 0x8000000000000005L`), `devintr()` returns `2`.
- **Per-Core Attribution**: In `usertrap()` and `kerneltrap()`, the kernel identifies the currently executing process `p = myproc()`.
- **Tick Increment**: If `p != 0`, `p->cputicks++` and `p->current_burst++` are incremented while holding `p->lock`.
- **Accuracy**: Accounts only for time actually spent on CPU cores.

---

## 7. Waiting-Time Calculation

- **Definition**: Waiting time is strictly the time spent by a process in the `RUNNABLE` state ready to execute while waiting for an available CPU core.
- **What is Counted**:
  - Time spent in `RUNNABLE` between being made ready (e.g. `kfork`, `wakeup`, or `yield`) and being chosen by `scheduler()`.
- **What is NOT Counted**:
  - `SLEEPING` time (waiting on I/O, timers, or locks).
  - `UNUSED` process slots.
  - `ZOMBIE` state (terminated processes waiting for parent `kwait()`).
  - `RUNNING` time (actively executing on CPU).
- **Implementation**: On each timer tick on CPU 0 in `clockintr()`, the function `clock_tick_accounting()` scans `proc[NPROC]` and increments `p->waitticks++` for all processes where `p->state == RUNNABLE`.

---

## 8. Context-Switch vs Scheduler Selection Definition

- **`sched_count` (Scheduler Selections)**: Incremented in `scheduler()` when a CPU selects a `RUNNABLE` process, marks it `RUNNING`, and switches into its context via `swtch(&c->context, &p->context)`.
- **`switches` (Context Switches)**: Incremented in `sched()` when an executing process relinquishes the CPU (due to timer preemption `yield()`, blocking `sleep()`, or termination `kexit()`) and switches back to the CPU scheduler context `swtch(&p->context, &mycpu()->context)`.

---

## 9. CPU-Burst Estimation Formula

In classical operating systems theory, a **CPU burst** is the duration a process computes between voluntary I/O or sleep requests. When a process is preempted by a timer interrupt, its CPU burst has not finished—it is simply interrupted.

When the burst completes (process enters `SLEEPING` or `ZOMBIE`):
1. The actual burst duration is recorded: $\text{actual\_burst} = \text{current\_burst}$.
2. The estimated CPU burst is updated using an Exponential Moving Average (EMA) with smoothing parameter $\alpha = 0.5$:
$$\text{est\_burst}_{\text{new}} = \begin{cases} \text{actual\_burst}, & \text{if } \text{est\_burst}_{\text{old}} = 0 \\ \frac{\text{actual\_burst} + \text{est\_burst}_{\text{old}}}{2}, & \text{otherwise} \end{cases}$$
3. Integer division ensures low-overhead kernel-safe computation without floating-point instructions or libraries.
4. `current_burst` is reset to 0.

*Real-Time Dynamic Preemption Tracking*: If a CPU-bound process continues computing across many timer slices without blocking, `current_burst` will steadily grow. If `current_burst > est_burst`, `est_burst` is updated immediately to `current_burst` so that compute-heavy tasks are classified as `CPU_BOUND` in real time.

---

## 10. Process-Classification Logic

Processes are classified deterministically based on their observed CPU burst behavior:

| Classification | Enum Identifier | Condition | Description |
| :--- | :--- | :--- | :--- |
| **INTERACTIVE** | `PROC_TYPE_INTERACTIVE (0)` | $\text{est\_burst} \le 2$ ticks | Fast-reacting processes (e.g. `sh`, text editors, short I/O tasks) that quickly yield/sleep after short compute bursts. |
| **MIXED** | `PROC_TYPE_MIXED (2)` | $2 < \text{est\_burst} < 8$ ticks | Workloads exhibiting moderate compute phases interlaced with periodic I/O or pause phases. |
| **CPU_BOUND** | `PROC_TYPE_CPU_BOUND (1)` | $\text{est\_burst} \ge 8$ ticks | Compute-intensive workloads that consume long CPU bursts and repeatedly exhaust time slices. |

The thresholds are defined as constants in `kernel/pstat.h`:
```c
#define INTERACTIVE_BURST_THRESH 2
#define CPU_BOUND_BURST_THRESH   8
```

---

## 11. System-Call Interface

Two clean system calls are added:

### 1. `int getprocinfo(int max_entries, struct procinfo *buf)`
- **Syscall Number**: `SYS_getprocinfo (23)`
- **Function**: Copies process monitoring structures for all active (`state != UNUSED`) processes into the user-provided array `buf`.
- **Return Value**: Total number of active processes on success, or `-1` on error / invalid user pointer.

### 2. `int getworkload(struct workload_info *winfo)`
- **Syscall Number**: `SYS_getworkload (24)`
- **Function**: Copies system-wide aggregate workload metrics into `winfo`.
- **Return Value**: `0` on success, or `-1` on error.

---

## 12. User Program: `monitor`

`user/monitor.c` provides a clean dashboard for inspecting process state:

```
==============================================================================
                         ADAPTIVE XV6 PROCESS MONITOR                         
==============================================================================
PID    PPID   NAME            STATE       CPU   WAIT  SWITCHES  BURST  TYPE       
------------------------------------------------------------------------------
     1      0 init            SLEEPING        0     1        27      0 INTERACTIVE
     2      1 sh              SLEEPING        1     1        24      0 INTERACTIVE
     9      2 monitor         RUNNING         0     0         7      0 INTERACTIVE
     4      1 cpu_test        RUNNING       138     0       147    138 CPU_BOUND  
     6      1 sleep_test      SLEEPING        1   118       129      0 INTERACTIVE
     8      1 mixed_test      RUNNING        30    68       103      1 INTERACTIVE
------------------------------------------------------------------------------
WORKLOAD SUMMARY:
  Total Active: 6 | Runnable: 0 | Running: 3 | Sleeping: 3
  Avg Est Burst: 23 ticks | Interactive: 83% | CPU-Bound: 16% | Mixed: 0%
==============================================================================
```

### Usage
- `monitor`: Runs a single inspection snapshot.
- `monitor <iterations> <delay_ticks>`: Refreshes the display periodically (e.g. `monitor 5 10` takes 5 snapshots with 10 ticks interval).

---

## 13. Testing Methodology & Verification Results

1. **Clean Build**: Verified with `make clean && make fs.img && make` with `-Wall -Werror` producing 0 compiler warnings and 0 errors.
2. **QEMU Boot**: Successfully boots 3-core SMP xv6 RISC-V environment.
3. **Workload Validation**:
   - `cpu_test`: Confirmed accumulation of high CPU ticks, rising burst estimates, and classification as `CPU_BOUND`.
   - `sleep_test`: Confirmed accumulation of waiting/sleeping ticks, low CPU burst, and classification as `INTERACTIVE`.
   - `mixed_test`: Confirmed balanced CPU burst and wait accumulation.
4. **Process Lifecycle**: Verified that process spawning (`fork`), termination (`exit`), and process cleanup (`wait`, `kill`) function correctly without memory leaks or zombie accumulation.
5. **Backwards Compatibility**: Core xv6 tools (`ls`, `cat`, `echo`, `mkdir`, `rm`, `kill`, `sh`) run without regression.

---

## 14. Known Limitations (Phase 1)

1. **Coarse Tick Granularity**: CPU accounting is clocked by RISC-V timer ticks (~100ms per tick in QEMU default). Sub-tick execution is rounded down until the next timer tick.
2. **Deterministic Thresholds**: Thresholds between Interactive, Mixed, and CPU-Bound are fixed constants ($2$ and $8$) rather than dynamic quantiles.
3. **Single Runqueue**: The baseline xv6 scheduler continues to scan a single linear process table `proc[NPROC]`.

---

## 15. How Phase 1 Prepares xv6 for Phase 2

Phase 1 provides all foundational metrics and data structures necessary for Phase 2:
- The **Workload Analyzer** (`get_workload_stats`) delivers high-level system indicators (`pct_interactive`, `pct_cpu_bound`, `num_runnable`).
- The **Process Classification Engine** provides real-time per-process labels (`PROC_TYPE_INTERACTIVE`, `PROC_TYPE_CPU_BOUND`, `PROC_TYPE_MIXED`).
- In Phase 2, these metrics will be directly consumed by an **Adaptive Scheduler** to dynamically switch policies (e.g., Shortest-Job-First for CPU-bound bursts, Multi-Level Feedback Queues for mixed workloads, and Priority boost for interactive tasks).
