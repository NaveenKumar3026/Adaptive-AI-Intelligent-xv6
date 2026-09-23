# Adaptive Intelligent xv6 Kernel — Phase 3 Design Specification

## 1. Executive Summary & Problem Statement

### 1.1 Problem Statement
While Phase 1 established deterministic process monitoring and Phase 2 introduced adaptive dynamic priority scheduling and variable time-slicing, standard OS schedulers remain strictly reactive. They respond to past observations without forecasting future CPU demands or detecting pathological anomalies (such as sudden burst surges, resource thrashing, process instability, or localized starvation). 

### 1.2 Phase 3 Objectives
Phase 3 establishes an **Intelligent In-Kernel Prediction, Anomaly Detection & Safe Self-Healing Subsystem** in xv6-riscv.

Key Objectives:
1. **Lightweight In-Kernel Workload Prediction**: Predict the duration of upcoming CPU execution bursts using a trend-aware linear integer Exponential Moving Average (Double EMA).
2. **Deterministic Anomaly Detection**: Monitor real-time process behavior against predictive baselines to detect burst surges, starvation conditions, behavioral thrashing, and erratic oscillations.
3. **Safe, Non-Destructive Self-Healing**: Automatically apply stabilizing kernel interventions (priority boost anti-starvation, trend damping, time-slice re-anchoring) without killing processes or corrupting memory.
4. **Observable Telemetry**: Export prediction accuracy, anomaly scores, and health classifications directly to user space via `getprocinfo()` / `getworkload()` and display them in `monitor`.

---

## 2. Prediction Engine Architecture

```
                                  +---------------------------------------+
                                  |    Voluntary Yield / Sleep / Exit     |
                                  +---------------------------------------+
                                                      |
                                                      v
+---------------------------------------------------------------------------------------------------+
|                                     IN-KERNEL PREDICTION ENGINE                                   |
|                                                                                                   |
|  1. Error Calculation:                                                                            |
|     error = |actual_burst - predicted_burst|                                                      |
|                                                                                                   |
|  2. Trend & Level Estimation:                                                                     |
|     current_diff = actual_burst - est_burst                                                       |
|     burst_trend  = (current_diff + burst_trend) / 2                                               |
|                                                                                                   |
|  3. Next Burst Forecast:                                                                          |
|     predicted_burst = max(1, est_burst + burst_trend)                                             |
+---------------------------------------------------------------------------------------------------+
```

### 2.1 Formula & Integer Arithmetic
- **Level Estimate**: Baseline EMA $\text{est\_burst} = (\text{actual} + \text{est\_burst}_{\text{old}}) / 2$.
- **Trend Estimate**: Velocity $\text{burst\_trend} = ((\text{actual} - \text{est\_burst}) + \text{burst\_trend}_{\text{old}}) / 2$.
- **Next Burst Forecast**: $\text{predicted\_burst} = \max(1, \text{est\_burst} + \text{burst\_trend})$.
- **Prediction Error**: $\text{prediction\_error} = |\text{actual} - \text{predicted\_burst}|$.

All calculations use pure integer arithmetic with zero floating-point operations.

---

## 3. Anomaly Detection Engine

### 3.1 Anomaly Signals & Bitmask Flags
The kernel tracks four distinct anomaly conditions:

| Flag | Identifier | Trigger Condition | Rationale |
| :--- | :--- | :--- | :--- |
| `0x01` | `ANOMALY_BURST_SURGE` | $\text{prediction\_error} \ge 4$ AND $\text{actual} > 2 \times \text{predicted}$ | Sudden unexpected jump in CPU burst length. |
| `0x02` | `ANOMALY_STARVATION` | $\text{waitticks} \ge 20$ ticks | Process ready in `RUNNABLE` but deprived of CPU execution. |
| `0x04` | `ANOMALY_THRASHING` | $\text{switches} > 2 \times \text{sched\_count}$ | Process constantly context switching without productive burst completion. |
| `0x08` | `ANOMALY_INSTABILITY` | $\text{prediction\_error} \ge 6$ ticks | Erratic, unpredictable execution behavior. |

### 3.2 Composite Anomaly Scoring
$$\text{Score}(p) = \min(100, (\text{prediction\_error} \times 3) + \sum \text{SignalWeights})$$

### 3.3 Process Health Classification
- **`HEALTH_NORMAL (0)`**: $\text{Score} < 30$. Process behavior matches historical baseline.
- **`HEALTH_WARNING (1)`**: $30 \le \text{Score} < 70$. Mild behavior drift or transient delay.
- **`HEALTH_CRITICAL (2)`**: $\text{Score} \ge 70$. Severe starvation or extreme burst surge.

---

## 4. Safe Self-Healing Mechanisms

Interventions are strictly stabilizing and non-destructive:

```
+---------------------------------------------------------------------------------------------------+
|                                      SELF-HEALING CONTROLLER                                      |
+---------------------------------------------------------------------------------------------------+
|  [Starvation Detected]       -->  Boost Priority (+25), Allocate Safe 2-tick Slice, Reset Wait    |
|  [Burst Surge / Instability] -->  Reset Runaway Trend, Re-anchor Prediction, Align Slice to Type   |
|  [Thrashing Detected]        -->  Expand Slice to 2 ticks to Prevent Rapid Yield Churn             |
|  [Score Relief]              -->  Decrement Anomaly Score (-15), Timestamp last_heal_tick         |
+---------------------------------------------------------------------------------------------------+
```

1. **Starvation Healing**: Automatically triggered in real time during `clock_tick_accounting()` or on scheduling pass. Grants $+25$ priority points and allocates at least 2 ticks to ensure forward progress.
2. **Trend & Burst Damping**: Damps runaway trend predictions back to baseline EMA.
3. **Thrashing Prevention**: Increases time slice to reduce context-switching thrash.

---

## 5. Locking, Concurrency, and SMP Safety

- **Process Locking**: All prediction updates and self-healing modifications are performed with `p->lock` held.
- **Invariant Guarantee**: `mycpu()->noff == 1` is preserved during scheduling passes and context switches.
- **Multi-Core Synchronization**: Real-time starvation detection executes during CPU 0 clock interrupts, acquiring `p->lock` per entry without global lock contention.
