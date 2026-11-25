# PROMETHEUS MVP – Tasks

> Status synchronized with the live codebase (`145/145` CTest tests passing on `windows-mingw-debug`).

---

## Phase 0 · Contracts and skeleton

### [x] TASK-0.1 · Freeze shared contracts
- **Files:** `prometheus-docs/interfaces.md`, `include/prometheus/common/{HostObservation,OrganismState,ComputeBudget,OrganismSnapshot}.hpp`
- **Status:** Ratified and frozen at `kContractVersion = 2`.

### [x] TASK-0.2 · CMake skeleton and module libraries
- **Files:** `CMakeLists.txt`, `CMakePresets.json`, `tests/CMakeLists.txt`
- **Status:** Static libraries for `core`, `sensory`, `platform_windows`, `brain`, `metabolism`, `body`, `memory`, `ui`, plus `prometheus_app` and 5 GoogleTest targets (`FetchContent` v1.15.2).

### [x] TASK-0.3 · Initial providers & stubs
- **Status:** Superseded by real implementations across all modules.

### [ ] TASK-0.4 · Verify Kiroween rules
- **Files:** `prometheus-docs/hackathon-checklist.md`
- **Status:** Open — official Kiroween 2026 rules/rubric/deadline remain `TBD` until verified.

---

## Phase 1 · Foundations

- [x] **TASK-1.1 · `CpuMonitor`** (`src/platform/windows/CpuMonitor.cpp`) — Win32 PDH interval sampling; first call returns `valid=false`.
- [x] **TASK-1.2 · `MemoryMonitor`** (`src/platform/windows/MemoryMonitor.cpp`) — `GlobalMemoryStatusEx` fraction + MB headroom; failure returns `valid=false`.
- [x] **TASK-1.3 · `SyntheticProvider`** (`src/sensory/SyntheticProvider.cpp`) — Scripted telemetry curves tagged `TelemetrySource::Synthetic`.
- [x] **TASK-1.4 · Brain state equations & EMA** (`src/brain/EmaFilter.cpp`, `PressureEstimator.cpp`, `HomeostasisEngine.cpp`, `tests/brain/`) — Pure C++20, bounded `[0,1]`, deterministic.
- [x] **TASK-1.5 · Behaviour state machine** (`src/brain/BehaviourFSM.cpp`, `tests/brain/test_behaviour_fsm.cpp`) — 6-state priority cascade with hysteresis & dwell locks.
- [x] **TASK-1.6 · Transparent Qt window & ViewModel** (`src/app/main.cpp`, `src/ui/OrganismViewModel.cpp`, `resources/qml/Main.qml`).
- [x] **TASK-1.7 · Fixed-capacity `ParticleSystem`** (`src/body/ParticleSystem.cpp`, `tests/body/test_particle_system.cpp`) — `kMaxParticles = 1000`, O(1) swap-with-last.
- [x] **TASK-1.8 · Independent simulation & render clocks** (`src/core/SimulationThread.cpp`, `src/core/RuntimeScheduler.cpp`).

---

## Phase 2 · Core survival logic

- [x] **TASK-2.1 · `SensorWorker` + real/synthetic adapters** (`src/sensory/SensorWorker.cpp`, `RealSensorProvider.cpp`).
- [x] **TASK-2.2 · `Metabolism` + `QualityTier` + hard limits** (`src/metabolism/Metabolism.cpp`, `tests/metabolism/test_metabolism.cpp`).
- [x] **TASK-2.3 · Body obeys `ComputeBudget`** (`src/body/ParticleSystem.cpp`, `src/core/SimulationThread.cpp`).
- [x] **TASK-2.4 · Versioned SQLite schema + `MemoryManager`** (`src/memory/MemoryManager.cpp`, `tests/memory/test_memory_manager.cpp`).
- [x] **TASK-2.5 · Immutable snapshot publishing** (`src/core/RuntimeCoordinator.cpp`, `src/ui/OrganismViewModel.cpp`).

---

## Phase 3 · Integration

- [x] **TASK-3.1 · Wire real/synthetic sensors through Coordinator** (`src/core/RuntimeCoordinator.cpp`, `--synthetic` flag).
- [x] **TASK-3.2 · Dormancy end-to-end** (`src/core/SimulationThread.cpp` 0.2 Hz heartbeat + UI animation/timer gating).
- [x] **TASK-3.3 · Persistence hooks** (save on dormancy entry/exit, transitions, 60 s periodic timer, clean shutdown; load on startup).
- [x] **TASK-3.4 · Synthetic telemetry label in UI** (`SYNTHETIC` badge + diagnostics indicator in `Main.qml`).

---

## Phase 4 · Validation and demo

- [x] **TASK-4.1 · Benchmark harness** (`scripts/benchmark.ps1`, `scripts/stress-worker.ps1`) — implemented; formal measured runs on target machine remain `TBD`.
- [x] **TASK-4.2 · Fixed-complexity mode** (`--fixed-tier N` flag in `src/app/main.cpp` and `Metabolism::setFixedTier`).
- [x] **TASK-4.3 · Developer diagnostics panel** (`[D]` / right-click overlay in `resources/qml/Main.qml`).
- [x] **TASK-4.4 · Survival Test demo flow** (`scripts/demo-stress.ps1`, `prometheus-docs/demo-script.md`).
- [ ] **TASK-4.5 · Dedicated `tests/sensory/` fault-injection suite & calibrated tuning** — Brain/Metabolism/Memory/Body/UI suites pass (145 tests); dedicated `tests/sensory/` binary and measured threshold tuning remain open.

---

## Phase 5 · Companion Skins, Animations & Moving Locomotion (UI Layer)

- [x] **TASK-5.1 · Mate-Engine–inspired animation state machine** (`OrganismViewModel::animState`, `animPulse`, `resources/qml/AnimationLayer.qml`, `tests/ui/test_organism_viewmodel.cpp`) — behaviour×tier postures, pressure flinch, explore dance, stress tremor, idle sub-cycle.
- [x] **TASK-5.2 · Companion skin selector & custom assets** (`resources/qml/CharacterPanel.qml`, `resources/characters/{owl,fox,bot,slime}.svg`, `QSettings` persistence).
- [x] **TASK-5.3 · Moving animations & desktop locomotion** (`include/prometheus/ui/LocomotionEngine.hpp`, `src/ui/LocomotionEngine.cpp`, `tests/ui/test_locomotion_engine.cpp`, `AnimationLayer.qml`, `Main.qml`) — autonomous desktop walking (`IdleWander` / `ExplorePatrol`), procedural gait + squash-and-stretch + facing flip + per-character personalities, damped spring drag sway, cursor gaze (`ObserveGaze`), monitor-edge peek, taskbar sitting, ground shadow, and `[M]` toggle.
