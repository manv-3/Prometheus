# Roadmap and 3-Developer Plan

The report's phases (Foundation → Senses → Brain → Survival → Memory → Validation) are sequential. To parallelize, we split by **module ownership** and freeze interfaces first.

Recommended build order: prove a Windows app can run a bounded simulation with a clean C++/QML boundary; then add telemetry, state, resource control and persistence. **Do not start with ML or evolution.**

## Roles

| | Dev A: Senses & Memory | Dev B: Brain & Metabolism | Dev C: Body, UI & Runtime |
|---|---|---|---|
| Owns | `sensory/`, `platform/windows/`, `memory/`, benchmarks | `brain/`, `metabolism/` | `app/`, `core/`, `body/`, `ui/`, CMake/Qt |
| Role | Provides the evidence | Holds the core idea | Critical path and integrator |

## Phase 0: Contracts and skeleton (all, together) · HARD GATE

- [x] Freeze contracts in `include/prometheus/common/` & [interfaces.md](interfaces.md): `HostObservation`, `OrganismState`, `Behaviour`, `QualityTier`, `ComputeBudget`, `OrganismSnapshot` (`kContractVersion = 2`)
- [x] Resolve open decisions in [decisions.md](decisions.md) (Behaviour naming, pressure ownership, error reporting, SQLite approach, Qt version)
- [x] Cross-cutting rules: time units, error reporting, snapshot thread-safety
- [x] **Dev C:** CMake + Qt skeleton, static library targets per module (`CMakeLists.txt`, `CMakePresets.json`)
- [x] **Dev A / B:** initial providers (`SyntheticProvider`) and deterministic Brain/Metabolism modules
- [x] GoogleTest (`FetchContent` v1.15.2), `ctest` targets, branch conventions
- [ ] **Verify Kiroween rules** ([hackathon-checklist.md](hackathon-checklist.md))

**Exit:** empty project builds on all three machines; interface headers merged.

## Phase 1: Foundations (parallel)

| Dev A | Dev B | Dev C |
|---|---|---|
| [x] `CpuMonitor` (Win32 PDH interval query) | [x] `HomeostasisEngine` state equations | [x] Transparent frameless Qt window + SVG/QML visual (`Main.qml`) |
| [x] `MemoryMonitor` (`GlobalMemoryStatusEx`) | [x] `EmaFilter` + `PressureEstimator` (renormalize missing) | [x] Fixed-capacity `ParticleSystem` |
| [x] `HostObservation` with validity + `steady_clock` | [x] Unit tests: bounds, determinism | [x] Separate simulation and render clocks (`SimulationThread`, `RuntimeScheduler`) |
| [x] **Synthetic telemetry** (`SyntheticProvider`) | | [x] Narrow `OrganismViewModel` (read-only `Q_PROPERTY`s) |

**Exit:** real and synthetic observations validated; organism renders and simulates decoupled from UI; Brain unit tests pass.

## Phase 2: Core survival logic (parallel)

| Dev A | Dev B | Dev C |
|---|---|---|
| [x] `SensorWorker` + mutex-protected latest snapshot | [x] `BehaviourFSM` with hysteresis + dwell | [x] `RuntimeCoordinator` + `RuntimeScheduler` |
| [x] Stale-data detection + failure fallbacks | [x] `Metabolism` + `QualityTier` 0–4 | [x] Body obeys `ComputeBudget::max_particles` |
| [x] Versioned SQLite schema + `MemoryManager` | [x] Degrade-fast / recover-slow logic | [x] Immutable snapshot publishing (`shared_ptr<const OrganismSnapshot>`) |
| | [x] Hard safety limits clamp | |

**Exit:** under synthetic pressure, behaviour changes and particle count and frame rate visibly drop.

## Phase 3: Integration (joint sprint)

- [x] Dev C wires real monitor (`RealSensorProvider`) + Brain/Metabolism through `SimulationThread`
- [x] **Dormancy:** B defines entry/exit; C parks `SimulationThread` on a 0.2 Hz heartbeat and gates UI animations/timers when dormant
- [x] **Persistence hooks:** save on dormancy entry/exit, behaviour transitions, periodic 60s timer, clean shutdown; restore on startup
- [ ] End-to-end automated integration test suite (unit suites for all modules currently pass 145/145)

**Exit:** healthy → dormant → recovered works end to end; identity survives restart.

## Phase 4: Validation, Companion UI & Moving Animations (parallel)

| Dev A | Dev B | Dev C |
|---|---|---|
| [x] Benchmark harness (`scripts/benchmark.ps1`) | [ ] Tune thresholds/tiers from measurements | [x] Diagnostics overlay (`[D]` / right-click in `Main.qml`) |
| [x] Controlled stress workload (`scripts/stress-worker.ps1`) | [x] `--fixed-tier N` baseline mode | [x] Survival Test demo script (`scripts/demo-stress.ps1`) |
| [ ] Dedicated `tests/sensory/` fault-injection target (covered inside Brain/UI tests for now) | [x] Boundary + regression tests (`prometheus_brain_tests`, `prometheus_metabolism_tests`) | [x] Companion skins (`CharacterPanel.qml`), Mate-Engine–inspired `AnimationLayer.qml`, and `LocomotionEngine` ([animation-spec.md](animation-spec.md)) |
| *Stretch:* NVML GPU telemetry | *Stretch:* adaptation statistics | *Stretch:* `ShaderEffect` greyscale dormancy |

**Exit:** adaptive vs fixed benchmark results captured on a documented machine (`TBD`); every demo claim backed by a measurement.

## Sync points

- End of Phase 0: contract review (hard gate).
- End of each phase: 30-minute integration check; everyone merges and runs the full app.
- Mid-phase: shared-header changes need both other devs' approval.

## Priority order (from the report)

1. CMake C++20 + Qt 6/QML foundation; transparent window shows a basic organism
2. Bounded simulation + independent render loop
3. CPU/RAM observation adapter + synthetic mode
4. Deterministic organism state + compute-budget controller
5. Persistence + Survival Test benchmark

## Deferred (post-MVP)

GPU telemetry · genetic evolution · reinforcement learning · LLM personality (excluded from core) · complex 3D rendering · multi-organism ecosystem.
