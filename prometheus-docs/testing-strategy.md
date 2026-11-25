# Testing Strategy

Goal: prove the causal chain **pressure → behaviour → budget → bounded workload**, and that the system is deterministic, bounded and recoverable.

## 1. Principles

- Brain and Metabolism are pure logic, tested without Qt, Windows or real telemetry.
- Synthetic telemetry drives deterministic tests and demos.
- Tests assert **invariants and boundaries**, not only happy paths.
- Fault injection is first-class, not an afterthought.

## 2. Unit tests

| Area | Tests |
|---|---|
| State bounds | Energy, health, stress, curiosity, adaptation stay within 0–100 under extreme inputs |
| Pressure | Missing signals renormalize (not zero); weights sum correctly; EMA responds fast up, slow down |
| Stress | Rises under sustained pressure; decays when safe |
| State machine | Hysteresis honoured; minimum dwell enforced; no flapping on noisy sequences; boundary values |
| Dormancy | Critical energy ⇒ Dormant; Dormant prevents normal simulation updates |
| Budget | Never exceeds `HardLimits` for any state/observation (property-based or exhaustive grid); degrade immediate, recover one tier at a time |
| Determinism | Identical state + observation sequence + time deltas ⇒ identical decisions (replay test) |
| Particle pool | Capacity fixed; active count ≤ budget; no allocation after init |
| Persistence | Save/load round-trip; schema version handling |
| UI & Locomotion | `animState` mapping, `animPulse` spike/decay, dormancy suppression, `LocomotionEngine` determinism/edge-turn/spring-sway/gaze/taskbar-sit, character & locomotion `QSettings` persistence |

## 3. Integration tests

| Scenario | Expectation |
|---|---|
| High synthetic RAM pressure | Reaches Brain as a **valid** observation |
| Sustained pressure | Brain selects Conserve |
| Behaviour change | Metabolism lowers the budget tier |
| New budget | Body respects particle and update limits |
| Dormancy entry | Checkpoint saved first; simulation ticks stop; render stops/event-driven |
| Recovery | Pressure falls ⇒ gradual tier restore; identity unchanged |
| Restart | Same `id`, age continuity, essential state restored |

## 4. Fault-injection tests

- NVML unavailable / no GPU
- Invalid, stale, delayed or missing sensor values
- Sudden memory-pressure spike
- Database write failure; corrupted or incomplete checkpoint
- Application shutdown **during** a state transition
- Recovery after prolonged dormancy
- Telemetry flapping around thresholds (oscillation guard)

## 5. Synthetic telemetry scripts

Provide named, reusable curves: `steady-low`, `ramp-up`, `spike`, `plateau-high`, `ramp-down`, `noisy-around-threshold`, `stale`, `dropout`, `gpu-missing`. Same scripts serve unit tests, integration tests, demos and benchmarks.

## 6. Tooling and CI

- GoogleTest (`v1.15.2` via `FetchContent`) for C++ unit tests; run via `ctest --preset windows-mingw-debug --output-on-failure`.
- Current CTest executables (`tests/CMakeLists.txt`, 145 tests total):
  - `prometheus_brain_tests` (`test_ema_filter.cpp`, `test_pressure_estimator.cpp`, `test_homeostasis_engine.cpp`, `test_behaviour_fsm.cpp`)
  - `prometheus_body_tests` (`test_particle_system.cpp`)
  - `prometheus_metabolism_tests` (`test_metabolism.cpp`)
  - `prometheus_memory_tests` (`test_memory_manager.cpp`)
  - `prometheus_ui_tests` (`test_organism_viewmodel.cpp`, `test_locomotion_engine.cpp`)
- Visual/UI behaviour checked manually via `--synthetic` plus the `[D]` diagnostics panel.

## 7. Exit gates

| Phase | Gate |
|---|---|
| 2 (Senses) | Real and synthetic observations validated |
| 3 (Brain) | Deterministic unit tests pass |
| 4 (Survival) | Workload measurably changes under injected pressure |
| 5 (Memory) | Identity survives restart and dormant recovery |
| 6 (Validation) | Claims backed by reproducible measurements |
