# Adaptive Intelligent xv6 Kernel — Phase 3 Documentation

## 1. Project Overview & Objectives

**Phase 3 Focus**: Intelligent Workload Prediction, Deterministic Anomaly Detection, and Safe In-Kernel Self-Healing.

Phase 3 builds directly upon the monitoring infrastructure of Phase 1 and the adaptive dynamic priority scheduler of Phase 2. It introduces forward-looking intelligence to xv6-riscv, enabling the kernel to:
1. Forecast future CPU burst lengths using Trend-Aware Double Exponential Smoothing.
2. Quantify prediction accuracy and detect behavioral anomalies (burst surges, starvation risks, thrashing).
3. Apply non-destructive self-healing interventions in real time to stabilize anomalous processes.
4. Expose full health telemetry to user-space utilities and monitoring tools.

---

## 2. Mathematical Formulation

### 2.1 Prediction Algorithm (Trend-Aware Double EMA)
$$\text{current\_diff} = \text{actual\_burst} - \text{est\_burst}$$
$$\text{burst\_trend} = \frac{\text{current\_diff} + \text{burst\_trend}_{\text{prev}}}{2}$$
$$\text{predicted\_burst} = \max(1, \text{est\_burst} + \text{burst\_trend})$$
$$\text{prediction\_error} = |\text{actual\_burst} - \text{predicted\_burst}|$$

### 2.2 Anomaly Scoring & Health Classification
$$\text{anomaly\_score} = \min\left(100, (\text{prediction\_error} \times 3) + \text{FlagsPenalty}\right)$$

| Health Status | Range | Description |
| :--- | :--- | :--- |
| `HEALTH_NORMAL` | $0 \le \text{Score} < 30$ | Process matches predictive model with minimal error. |
| `HEALTH_WARNING` | $30 \le \text{Score} < 70$ | Behavior shift, moderate prediction error, or transient wait accumulation. |
| `HEALTH_CRITICAL` | $70 \le \text{Score} \le 100$ | Severe starvation ($\ge 20$ ticks) or extreme burst surges. |

---

## 3. System Interfaces & Telemetry

### 3.1 Extended `struct procinfo`
Exposed via `getprocinfo()` system call:
- `uint predicted_burst`: Forecasted CPU burst duration in ticks.
- `uint prediction_error`: Absolute difference $|actual - predicted|$.
- `int anomaly_score`: Composite severity score $(0 - 100)$.
- `int anomaly_flags`: Bitmask of active anomaly reasons (`ANOMALY_BURST_SURGE`, `ANOMALY_STARVATION`, `ANOMALY_THRASHING`, `ANOMALY_INSTABILITY`).
- `int health_status`: `HEALTH_NORMAL`, `HEALTH_WARNING`, or `HEALTH_CRITICAL`.
- `uint healing_actions`: Total corrective actions applied to this process.
- `uint64 last_heal_tick`: Timestamp of most recent intervention.

### 3.2 Extended `struct workload_info`
Exposed via `getworkload()` system call:
- `int num_healthy`: Count of processes in `HEALTH_NORMAL`.
- `int num_warning`: Count of processes in `HEALTH_WARNING`.
- `int num_critical`: Count of processes in `HEALTH_CRITICAL`.
- `int total_healing_actions`: Total kernel interventions applied across all active processes.

---

## 4. User Programs & Testing Workloads

1. **`monitor`**: Real-time dashboard displaying `PRED`, `ERR`, `ANOM`, `HEALS`, and `HEALTH` alongside process state and summary diagnostics.
2. **`stable`**: Validates consistent low-error prediction under stationary workloads.
3. **`bursttest`**: Validates rapid trend tracking during alternating compute phases.
4. **`anomaly`**: Validates detection of erratic burst surges and subsequent self-healing damping.
5. **`starve`**: Validates real-time detection of high waiting times and automatic priority boost healing.

---

## 5. Verification & Testing

Run the automated test harness:
```bash
python test_phase3.py
```

Or run manually within QEMU:
```bash
stable &
bursttest &
anomaly &
starve &
monitor 5 10
```
