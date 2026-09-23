#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // Phase 1: Initialize process monitoring and scheduling metrics
  p->cputicks = 0;
  p->waitticks = 0;
  p->sched_count = 0;
  p->switches = 0;
  p->creation_tick = ticks;
  p->last_sched_tick = 0;
  p->current_burst = 0;
  p->last_burst = 0;
  p->est_burst = 0;
  p->proc_type = PROC_TYPE_INTERACTIVE;

  // Phase 2: Initialize priority and time slice
  p->priority = PRIO_BASE_INTERACTIVE;
  p->time_slice = 1;
  p->ticks_in_slice = 0;

  // Phase 3: Initialize Prediction, Anomaly Detection & Self-Healing
  p->predicted_burst = 1;
  p->prediction_error = 0;
  p->burst_trend = 0;
  p->anomaly_score = 0;
  p->anomaly_flags = ANOMALY_NONE;
  p->health_status = HEALTH_NORMAL;
  p->healing_actions = 0;
  p->last_heal_tick = 0;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  // Phase 1: Reset statistics on free
  p->cputicks = 0;
  p->waitticks = 0;
  p->sched_count = 0;
  p->switches = 0;
  p->creation_tick = 0;
  p->last_sched_tick = 0;
  p->current_burst = 0;
  p->last_burst = 0;
  p->est_burst = 0;
  p->proc_type = PROC_TYPE_INTERACTIVE;

  // Phase 2: Reset priority and time slice
  p->priority = PRIO_BASE_INTERACTIVE;
  p->time_slice = 1;
  p->ticks_in_slice = 0;

  // Phase 3: Reset prediction & health
  p->predicted_burst = 0;
  p->prediction_error = 0;
  p->burst_trend = 0;
  p->anomaly_score = 0;
  p->anomaly_flags = ANOMALY_NONE;
  p->health_status = HEALTH_NORMAL;
  p->healing_actions = 0;
  p->last_heal_tick = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0) {
    if (sz + n > TRAPFRAME) {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); //DOC: wait-sleep
  }
}

// Phase 2: Compute dynamic priority score for a process.
// Score = BaseTypeScore + (AgingBonus * waitticks) - (BurstPenalty * est_burst)
int
calc_dynamic_priority(struct proc *p)
{
  int base;
  if (p->proc_type == PROC_TYPE_INTERACTIVE)
    base = PRIO_BASE_INTERACTIVE;
  else if (p->proc_type == PROC_TYPE_MIXED)
    base = PRIO_BASE_MIXED;
  else
    base = PRIO_BASE_CPU_BOUND;

  int prio = base + (int)(p->waitticks * AGING_WEIGHT) - (int)(p->est_burst * BURST_PENALTY_WEIGHT);
  if (prio < MIN_PRIORITY)
    prio = MIN_PRIORITY;
  if (prio > MAX_PRIORITY)
    prio = MAX_PRIORITY;
  return prio;
}

// Phase 2: Compute allocated execution time slice (in ticks) based on process type.
// INTERACTIVE: 1 tick (responsive)
// MIXED:       2 ticks
// CPU_BOUND:   4 ticks (high throughput, fewer context switches)
uint
calc_time_slice(struct proc *p)
{
  if (p->proc_type == PROC_TYPE_INTERACTIVE)
    return 1;
  else if (p->proc_type == PROC_TYPE_MIXED)
    return 2;
  else
    return 4;
}

// Phase 3: Apply safe, non-destructive self-healing to stabilize anomalous processes
void
apply_self_healing(struct proc *p)
{
  int healed = 0;

  // Remedy 1: Starvation Mitigation - Boost Priority
  if (p->anomaly_flags & ANOMALY_STARVATION) {
    p->priority += 25;
    if (p->priority > MAX_PRIORITY)
      p->priority = MAX_PRIORITY;
    p->time_slice = 2; // Guarantee at least 2 ticks to make progress
    healed = 1;
  }

  // Remedy 2: Burst Surge & Instability Damping
  if (p->anomaly_flags & (ANOMALY_BURST_SURGE | ANOMALY_INSTABILITY)) {
    p->burst_trend = 0; // Dampen runaway linear trend
    p->predicted_burst = p->est_burst; // Re-anchor prediction to baseline EMA
    p->time_slice = calc_time_slice(p);
    healed = 1;
  }

  // Remedy 3: Thrashing Mitigation
  if (p->anomaly_flags & ANOMALY_THRASHING) {
    p->time_slice = 2; // Provide moderate slice to prevent frequent context switches
    healed = 1;
  }

  if (healed) {
    p->healing_actions++;
    p->last_heal_tick = ticks;
    // Anomaly score is partially relieved following corrective action
    if (p->anomaly_score > 20)
      p->anomaly_score -= 15;
    if (p->anomaly_score < 30)
      p->health_status = HEALTH_NORMAL;
    else if (p->anomaly_score < 70)
      p->health_status = HEALTH_WARNING;
  }
}

// Phase 3: Update workload prediction, calculate anomaly score, and classify health status
void
update_prediction_and_health(struct proc *p)
{
  uint actual = p->last_burst;

  // 1. Prediction error calculation
  if (p->predicted_burst > 0) {
    if (actual >= p->predicted_burst)
      p->prediction_error = actual - p->predicted_burst;
    else
      p->prediction_error = p->predicted_burst - actual;
  } else {
    p->prediction_error = 0;
  }

  // 2. Trend-Aware Integer Prediction Update
  int current_diff = (int)actual - (int)p->est_burst;
  p->burst_trend = (current_diff + p->burst_trend) / 2;

  int next_pred = (int)p->est_burst + p->burst_trend;
  if (next_pred < 1)
    next_pred = 1;
  p->predicted_burst = (uint)next_pred;

  // 3. Anomaly Detection
  int flags = ANOMALY_NONE;
  int score = 0;

  // Signal 1: Burst Surge (actual is much larger than predicted)
  if (p->prediction_error >= BURST_SURGE_THRESH && actual > (p->predicted_burst * 2)) {
    flags |= ANOMALY_BURST_SURGE;
    score += 35;
  }

  // Signal 2: Starvation (waiting too long in RUNNABLE without being scheduled)
  if (p->waitticks >= STARVATION_THRESH_TICKS) {
    flags |= ANOMALY_STARVATION;
    score += 40;
  }

  // Signal 3: Instability / High Prediction Error
  if (p->prediction_error >= 6) {
    flags |= ANOMALY_INSTABILITY;
    score += 25;
  }

  // Signal 4: Thrashing (rapid switches relative to schedule count)
  if (p->switches > (p->sched_count * 2) && p->sched_count > 5) {
    flags |= ANOMALY_THRASHING;
    score += 20;
  }

  score += (int)(p->prediction_error * 3);
  if (score > MAX_ANOMALY_SCORE)
    score = MAX_ANOMALY_SCORE;

  p->anomaly_flags = flags;
  p->anomaly_score = score;

  // 4. Health Classification
  if (p->anomaly_score >= 70)
    p->health_status = HEALTH_CRITICAL;
  else if (p->anomaly_score >= 30)
    p->health_status = HEALTH_WARNING;
  else
    p->health_status = HEALTH_NORMAL;

  // 5. Trigger Self-Healing if needed
  if (p->health_status != HEALTH_NORMAL || (flags & ANOMALY_STARVATION)) {
    apply_self_healing(p);
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a highest-dynamic-priority RUNNABLE process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    struct proc *best_p = 0;
    int best_prio = -1;

    // Pass 1: Scan RUNNABLE processes to select the highest dynamic priority
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        p->priority = calc_dynamic_priority(p);
        if (p->priority > best_prio) {
          best_prio = p->priority;
          best_p = p;
        }
      }
      release(&p->lock);
    }

    // Pass 2: Dispatch the best candidate if still RUNNABLE
    int dispatched = 0;
    if (best_p != 0) {
      acquire(&best_p->lock);
      if (best_p->state == RUNNABLE) {
        p = best_p;
        p->state = RUNNING;
        p->sched_count++;
        p->last_sched_tick = ticks;
        p->time_slice = calc_time_slice(p);
        p->ticks_in_slice = 0;
        p->waitticks = 0; // Reset wait time upon gaining CPU
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        dispatched = 1;
      }
      release(&best_p->lock);
    }

    if (!dispatched) {
      // nothing to run or candidate taken by another core;
      // stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  // Track context switch out of the process
  p->switches++;

  // CPU burst calculation & estimation
  if (p->state == SLEEPING || p->state == ZOMBIE) {
    // Burst is complete because the process gave up the CPU voluntarily (I/O sleep or exit)
    p->last_burst = p->current_burst;
    if (p->est_burst == 0) {
      p->est_burst = p->last_burst;
    } else {
      // Integer exponential moving average with alpha = 0.5: (actual + old_est) / 2
      p->est_burst = (p->last_burst + p->est_burst) / 2;
    }

    // Deterministic classification based on estimated burst length
    if (p->est_burst <= INTERACTIVE_BURST_THRESH)
      p->proc_type = PROC_TYPE_INTERACTIVE;
    else if (p->est_burst >= CPU_BOUND_BURST_THRESH)
      p->proc_type = PROC_TYPE_CPU_BOUND;
    else
      p->proc_type = PROC_TYPE_MIXED;

    // Phase 3: Update prediction, compute anomaly score, and classify health
    update_prediction_and_health(p);

    p->current_burst = 0;
  } else if (p->state == RUNNABLE) {
    // Process was preempted (e.g. timer interrupt yield).
    // The CPU burst is still ongoing; if current burst already exceeds the estimate,
    // update the estimate so the process is classified accurately in real time.
    if (p->current_burst > p->est_burst) {
      p->est_burst = p->current_burst;
      if (p->est_burst >= CPU_BOUND_BURST_THRESH)
        p->proc_type = PROC_TYPE_CPU_BOUND;
      else if (p->est_burst <= INTERACTIVE_BURST_THRESH)
        p->proc_type = PROC_TYPE_INTERACTIVE;
      else
        p->proc_type = PROC_TYPE_MIXED;

      p->last_burst = p->current_burst;
      update_prediction_and_health(p);
    }
  }

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src) {
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
    // clang-format off
    [UNUSED]    "unused",
    [USED]      "used",
    [SLEEPING]  "sleep ",
    [RUNNABLE]  "runble",
    [RUNNING]   "run   ",
    [ZOMBIE]    "zombie"
    // clang-format on
  };
  struct proc *p;
  char *state;

  printk("\n");
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printk("%d %s %s", p->pid, state, p->name);
    printk("\n");
  }
}

// Phase 1: Accounting called on every clock tick (CPU 0) to increment waiting time
// for all processes waiting in the RUNNABLE state.
void
clock_tick_accounting(void)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      p->waitticks++;
      // Phase 3: Real-time starvation detection & self-healing
      if (p->waitticks >= STARVATION_THRESH_TICKS && !(p->anomaly_flags & ANOMALY_STARVATION)) {
        p->anomaly_flags |= ANOMALY_STARVATION;
        p->anomaly_score = (p->anomaly_score + 40 > MAX_ANOMALY_SCORE) ? MAX_ANOMALY_SCORE : (p->anomaly_score + 40);
        if (p->anomaly_score >= 70)
          p->health_status = HEALTH_CRITICAL;
        else
          p->health_status = HEALTH_WARNING;
        apply_self_healing(p);
      }
    }
    release(&p->lock);
  }
}

// Phase 1 & 3: Retrieve process statistics for all active processes.
// Safely streams each procinfo entry directly to the user address space.
// Returns total active process count, or -1 on copyout failure.
int
get_proc_stats(uint64 dst_addr, int max_entries)
{
  struct proc *p;
  struct proc *curr = myproc();
  int count = 0;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED) {
      if (count < max_entries) {
        struct procinfo pi;
        pi.pid = p->pid;
        pi.ppid = p->parent ? p->parent->pid : 0;
        safestrcpy(pi.name, p->name, sizeof(pi.name));
        pi.state = p->state;
        pi.cputicks = p->cputicks;
        pi.waitticks = p->waitticks;
        pi.sched_count = p->sched_count;
        pi.switches = p->switches;
        pi.creation_tick = p->creation_tick;
        pi.last_sched_tick = p->last_sched_tick;
        pi.current_burst = p->current_burst;
        pi.last_burst = p->last_burst;
        pi.est_burst = p->est_burst;
        pi.proc_type = p->proc_type;
        pi.priority = p->priority;
        pi.time_slice = p->time_slice;
        pi.ticks_in_slice = p->ticks_in_slice;
        
        // Phase 3 fields
        pi.predicted_burst = p->predicted_burst;
        pi.prediction_error = p->prediction_error;
        pi.anomaly_score = p->anomaly_score;
        pi.anomaly_flags = p->anomaly_flags;
        pi.health_status = p->health_status;
        pi.healing_actions = p->healing_actions;
        pi.last_heal_tick = p->last_heal_tick;
        release(&p->lock);

        uint64 target = dst_addr + (uint64)count * sizeof(struct procinfo);
        if (copyout(curr->pagetable, target, (char *)&pi, sizeof(pi)) < 0)
          return -1;
      } else {
        release(&p->lock);
      }
      count++;
    } else {
      release(&p->lock);
    }
  }
  return count;
}

// Phase 1 & 3: Aggregate system workload statistics across all active processes.
int
get_workload_stats(struct workload_info *winfo)
{
  struct proc *p;
  int runnable = 0, running = 0, sleeping = 0, total = 0;
  int interactive = 0, cpu_bound = 0, mixed = 0;
  int healthy = 0, warning = 0, critical = 0, total_heals = 0;
  uint total_burst = 0;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED) {
      total++;
      if (p->state == RUNNABLE)
        runnable++;
      else if (p->state == RUNNING)
        running++;
      else if (p->state == SLEEPING)
        sleeping++;

      total_burst += p->est_burst;
      if (p->proc_type == PROC_TYPE_INTERACTIVE)
        interactive++;
      else if (p->proc_type == PROC_TYPE_CPU_BOUND)
        cpu_bound++;
      else if (p->proc_type == PROC_TYPE_MIXED)
        mixed++;

      if (p->health_status == HEALTH_NORMAL)
        healthy++;
      else if (p->health_status == HEALTH_WARNING)
        warning++;
      else if (p->health_status == HEALTH_CRITICAL)
        critical++;

      total_heals += p->healing_actions;
    }
    release(&p->lock);
  }

  winfo->num_runnable = runnable;
  winfo->num_running = running;
  winfo->num_sleeping = sleeping;
  winfo->num_total = total;
  winfo->avg_est_burst = total > 0 ? (total_burst / total) : 0;
  winfo->pct_interactive = total > 0 ? ((interactive * 100) / total) : 0;
  winfo->pct_cpu_bound = total > 0 ? ((cpu_bound * 100) / total) : 0;
  winfo->pct_mixed = total > 0 ? ((mixed * 100) / total) : 0;

  winfo->num_healthy = healthy;
  winfo->num_warning = warning;
  winfo->num_critical = critical;
  winfo->total_healing_actions = total_heals;

  return 0;
}

