# Adaptive Intelligent xv6 Kernel — Phase 2 Design Specification

## 1. Executive Summary & Problem Statement

### 1.1 Problem Statement
The standard xv6-riscv operating system utilizes a simple, static Round-Robin (RR) scheduling policy. Every process is treated identically with a fixed 1-tick execution quantum (~100 ms in QEMU default). This baseline scheduler exhibits significant limitations in realistic multi-process environments:
1. **Interactive Latency**: Interactive processes (e.g., shells, text editors, I/O-bound daemons) that only need fractions of a millisecond of CPU time to service an event are queued behind compute-heavy tasks and must wait for full round-robin cycles.
2. **Context Switch Overhead**: CPU-bound tasks are forcibly preempted every single tick, incurring excessive context switch overhead, cache invalidation, and register save/restore penalties.
3. **Workload Agnosticism**: The scheduler possesses no awareness of process characteristics or overall system load.

### 1.2 Phase 2 Objectives
Phase 2 transforms the xv6 scheduler into an **Adaptive, Workload-Aware, Intelligent Scheduler** leveraging the metrics collected in Phase 1 (`proc_type`, `est_burst`, `cputicks`, `waitticks`, `sched_count`, `switches`, `current_burst`, `last_burst`).

Key Objectives:
- **Interactive Responsiveness**: Fast-track interactive and I/O-bound processes to provide near-zero perceived latency.
- **CPU-Bound Throughput**: Grant larger continuous execution quanta (burst allocation) to CPU-bound processes to reduce unnecessary context switching.
- **Fairness & Starvation Prevention**: Implement dynamic priority aging so that long-waiting compute tasks never starve, even under high interactive load.
- **SMP Concurrency Safety**: Maintain full multi-core scalability across all active CPUs (default 3 cores) without coarse global locks or deadlock hazards.
- **Non-Destructive Integration**: Preserve all Phase 1 statistics, syscalls (`getprocinfo`, `getworkload`), and user-space tools (`monitor`).

---

## 2. Analysis of Existing xv6 Scheduler Architecture

### 2.1 Process Selection in Baseline xv6
In `kernel/proc.c`, the `scheduler()` loop executes continuously on every CPU core:
```c
void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;) {
    intr_on();
    intr_off();
    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        p->state = RUNNING;
        p->sched_count++;
        p->last_sched_tick = ticks;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      asm volatile("wfi");
    }
  }
}
```

### 2.2 Mechanism Characteristics
- **Policy**: Linear Round-Robin across the static table `proc[NPROC]` (64 entries).
- **Preemption**: Hardware timer interrupts fire periodically (`which_dev == 2` in `trap.c`). In `usertrap()` and `kerneltrap()`, timer interrupts invoke `yield()`, marking the process `RUNNABLE` and calling `sched()` to switch back to the per-CPU `scheduler()` context.
- **Process Locking**: Every process has an individual spinlock `p->lock`. A CPU must acquire `p->lock` before inspecting or altering `p->state`. When switching into a process, `p->lock` remains held by the CPU and is released by `forkret()` or immediately following `swtch()` in `yield()`/`sleep()`/`kexit()`.
- **SMP Behavior**: Multiple CPUs scan the shared `proc[NPROC]` array in parallel. Fine-grained locking on `p->lock` allows CPUs to pick different `RUNNABLE` processes simultaneously without lock contention on a global runqueue.
- **Phase 1 Accounting Hooks**:
  - `usertrap()` / `kerneltrap()`: Increment `p->cputicks` and `p->current_burst`.
  - `sched()`: Updates `p->switches`, calculates integer EMA CPU burst estimate (`p->est_burst`), and classifies process into `PROC_TYPE_INTERACTIVE`, `PROC_TYPE_CPU_BOUND`, or `PROC_TYPE_MIXED`.
  - `clock_tick_accounting()`: On CPU 0 timer interrupts, scans `proc[NPROC]` and increments `p->waitticks` for all `RUNNABLE` processes.

---

## 3. Comparison of Scheduling Approaches

| Approach | Implementation Complexity | Suitability for xv6 | Interactive Responsiveness | Fairness & Starvation Prevention | Use of Phase 1 Metrics | SMP Scalability |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **A. Adaptive Dynamic Priority with Aging & Dynamic Quanta** | **Moderate** | **Excellent** (Native fit to `proc[NPROC]`) | **High** (Interactive tasks receive instant priority boost) | **Guaranteed** (Aging ensures unbounded wait time cannot occur) | **Full** (`proc_type`, `est_burst`, `waitticks`, `cputicks`) | **High** (Per-process locks, no global lock bottleneck) |
| **B. Adaptive Round-Robin with Dynamic Quanta Only** | Low | Good | Low/Moderate (Order is still strictly circular) | High (Standard RR fairness) | Partial (`proc_type` used only for slice duration) | High |
| **C. Multilevel Feedback Queue (MLFQ)** | High | Complex (Requires explicit linked queues or stepped levels) | High | Requires periodic global priority boost | High | Moderate (Risk of queue lock contention across CPUs) |
| **D. Workload-Based Global Policy Switching** | Moderate | Fragile (Risk of policy thrashing & hysteresis) | Variable (Degrades during heavy batch phases) | Unpredictable across policy transitions | Macro-level (`get_workload_stats`) | High |

### Recommendation: Approach A (Adaptive Dynamic Priority Scheduler with Priority Aging & Dynamic Time Slicing)

**Rationale**:
1. **Algorithmic Elegance**: Operates directly on the established `proc[NPROC]` table without introducing fragile multi-queue pointer manipulations or memory allocations in kernel space.
2. **Comprehensive Metric Utilization**: Actively leverages Phase 1's `est_burst` for shortest-job preference, `proc_type` for base tier prioritization, `waitticks` for anti-starvation aging, and dynamic execution budgets for compute tasks.
3. **SMP Concurrency Safety**: Fully preserves xv6's lock-acquisition protocol (`acquire(&p->lock)` during candidate evaluation and dispatch).
4. **Academic & Practical Rigor**: Combines the theoretical advantages of Shortest Remaining Processing Time (SRPT), Multi-Level Priority, and Dynamic Slicing in a clean, provably starvation-free model.

---

## 4. Proposed Adaptive Scheduling Architecture

```
+-----------------------------------------------------------------------------------+
|                            TIMER INTERRUPT / TRAP LAYER                           |
|  * Track continuous CPU ticks in current slice (p->ticks_in_slice++)             |
|  * Preempt only when: (p->ticks_in_slice >= p->time_slice) OR (higher-prio ready) |
+-----------------------------------------------------------------------------------+
                                        |
                                        v
+-----------------------------------------------------------------------------------+
|                        ADAPTIVE DECISION ENGINE (Kernel)                          |
|                                                                                   |
|  1. Dynamic Priority Score Calculation:                                            |
|     Priority(p) = BaseTypeScore(p->proc_type)                                    |
|                 + AgingBonus(p->waitticks)                                        |
|                 - BurstPenalty(p->est_burst)                                      |
|                                                                                   |
|  2. Dynamic Time Slice Allocation:                                                |
|     * INTERACTIVE : 1 tick  (Immediate preemption check, high responsiveness)     |
|     * MIXED       : 2 ticks (Balanced throughput & latency)                       |
|     * CPU_BOUND   : 4 ticks (Reduced context switches, maximum compute efficiency)|
+-----------------------------------------------------------------------------------+
                                        |
                                        v
+-----------------------------------------------------------------------------------+
|                          PER-CPU SCHEDULER LOOP (SMP)                             |
|  * Each core evaluates candidate RUNNABLE processes under p->lock                 |
|  * Selects candidate with highest dynamic priority score                          |
|  * Dispatches winning process; resets p->ticks_in_slice and p->waitticks          |
+-----------------------------------------------------------------------------------+
```

### 4.1 Dynamic Priority Scoring Formula
To balance responsiveness and fairness, each `RUNNABLE` process is evaluated using a normalized integer score:

$$\text{Score}(p) = \text{BaseScore}(\text{proc\_type}) + \text{AgingWeight} \times \text{waitticks} - \text{BurstWeight} \times \text{est\_burst}$$

Constants configured in `kernel/pstat.h`:
- `PRIO_BASE_INTERACTIVE = 50`
- `PRIO_BASE_MIXED = 30`
- `PRIO_BASE_CPU_BOUND = 10`
- `AGING_WEIGHT = 2` (adds 2 score points per waiting tick)
- `BURST_PENALTY_WEIGHT = 1` (penalizes estimated burst length)

### 4.2 Dynamic Time Slice Allocation
When a process is selected by `scheduler()` or transitions to `RUNNING`:
```c
static inline uint compute_time_slice(struct proc *p) {
  if (p->proc_type == PROC_TYPE_INTERACTIVE)
    return 1; // 1 tick
  else if (p->proc_type == PROC_TYPE_MIXED)
    return 2; // 2 ticks
  else
    return 4; // 4 ticks for CPU-bound
}
```

In `usertrap()` and `kerneltrap()`:
```c
p->ticks_in_slice++;
if (p->ticks_in_slice >= p->time_slice) {
  p->ticks_in_slice = 0;
  yield();
}
```

### 4.3 Starvation Prevention & Priority Aging
As a CPU-bound process waits in `RUNNABLE` state, `clock_tick_accounting()` increments `p->waitticks`.
With `AGING_WEIGHT = 2`, a CPU-bound task (`BaseScore = 10`) waiting for 25 ticks accumulates:
$$\text{Score} = 10 + 2 \times 25 - 8 = 52 > 50$$
It naturally surpasses newly arrived interactive tasks (`Score = 50`), guaranteeing bounded waiting times and provable starvation prevention.

---

## 5. Process Lifecycle & Data Flow

1. **Process Allocation (`allocproc`)**:
   - Initializes Phase 2 fields: `p->priority = PRIO_BASE_INTERACTIVE`, `p->time_slice = 1`, `p->ticks_in_slice = 0`.
2. **Process Wakeup / Enqueue (`wakeup`, `kfork`, `yield`)**:
   - `p->state = RUNNABLE`.
   - `p->waitticks` continues accumulation via `clock_tick_accounting()`.
3. **Process Selection (`scheduler`)**:
   - CPU scans `proc[NPROC]` to identify the `RUNNABLE` process with the highest dynamic score.
   - Selected process is marked `RUNNING`, `p->time_slice` is computed, `p->ticks_in_slice = 0`, and `p->waitticks` is reset upon dispatch.
4. **Execution & Preemption (`usertrap` / `kerneltrap`)**:
   - On timer tick, `p->cputicks++`, `p->current_burst++`, and `p->ticks_in_slice++`.
   - If `p->ticks_in_slice >= p->time_slice`, preemption is triggered via `yield()`.
5. **State Transition (`sched`)**:
   - Evaluates EMA burst update and dynamic type classification.
   - Increments `p->switches++`.

---

## 6. Implementation Milestones

### Milestone 2A: Architecture Analysis & Specification (Current)
- Comprehensive code audit of `kernel/proc.c`, `proc.h`, `trap.c`, `defs.h`, `pstat.h`.
- Formal design document creation (`PHASE2_DESIGN.md`).
- Formulation of implementation plan and review artifact.

### Milestone 2B: Adaptive Decision Engine & Metric Extensions
- Extend `struct proc` and `struct procinfo` in `kernel/proc.h` and `kernel/pstat.h` with `priority`, `time_slice`, and `ticks_in_slice`.
- Implement `calculate_priority(struct proc *p)` helper in `kernel/proc.c`.
- Update `allocproc()` and `freeproc()` to initialize and clean up Phase 2 scheduling fields.

### Milestone 2C: Scheduler & Trap Handler Integration
- Update `usertrap()` and `kerneltrap()` in `kernel/trap.c` to enforce dynamic time-slice preemption.
- Update `scheduler()` in `kernel/proc.c` to execute highest-dynamic-priority candidate selection with safe spinlock handoffs.
- Preserve backward compatibility for `get_proc_stats()` and `get_workload_stats()`.

### Milestone 2D: Testing, Evaluation & Benchmarking
- Build automated stress and validation test suite (`test_phase2.py`).
- Run interactive response tests (`sleep_test`), throughput benchmarks (`cpu_test`), and mixed workload stress tests.
- Compare context switch counts, latency, and throughput metrics against Phase 1 baseline.

---

## 7. Locking, Concurrency, and Safety Considerations

1. **Deadlock Prevention**: All process state queries and priority calculations inside `scheduler()` are performed while holding the individual process lock `p->lock`. Locks are released before examining the next process unless chosen for context switch.
2. **Atomic Dispatch**: When the optimal candidate is found, the winning process's lock `p->lock` is held throughout `swtch()` and released in accordance with standard xv6 lock discipline.
3. **Interrupt Safety**: Interrupts are disabled (`intr_off()`) during scheduling decisions and context switching to prevent nested trap corruption.
4. **Rollback Safety**: The adaptive logic is modularized into discrete helper routines; the default Round-Robin loop can be restored instantaneously if needed.
