# Decision Log

Lightweight ADR log. Add an entry whenever a choice affects a contract, dependency, thread, persistent format or public claim.

## Open / Resolved decisions

| # | Decision | Options | Current Resolution | Owner | Status |
|---|---|---|---|---|---|
| D1 | `Behaviour` naming | Active/Idle/Stressed/Recovery/Dormant vs Explore/Idle/Conserve/Recover/Dormant | Ratified in `OrganismState.hpp`: `Dormant, Resting, Idle, Observing, Exploring, Stressed` | B | Decided |
| D2 | Where smoothed pressure lives | In `OrganismState` vs recomputed in Metabolism | `PressureEstimator` produces `PressureResult`; `SimulationThread` passes `smoothed_pressure` into `Metabolism` and `OrganismSnapshot` | B | Decided |
| D3 | Error reporting | Exceptions vs return values/validity flags vs result type | Validity flags (`cpu_valid`, `memory_valid`) for sensors; `LoadResult` / booleans for Memory I/O; no exceptions in hot loop | All | Decided |
| D4 | Snapshot publication | `shared_ptr<const T>` + queued signal vs atomic swap | `std::shared_ptr<const OrganismSnapshot>` via `Qt::QueuedConnection` | C | Decided |
| D5 | CPU telemetry source | PDH vs `GetSystemTimes` deltas | Win32 PDH (`CpuMonitor.cpp` interval query; first call returns `valid=false`) | A | Decided |
| D6 | SQLite integration | Amalgamation vs vcpkg vs Qt SQL | Direct `sqlite3` (`SQLite3` package or MinGW opt `sqlite3`) | A | Decided |
| D7 | Qt version pin | Qt 6 LTS release | Qt 6 (`6.12.0` MinGW kit configured in `CMakePresets.json`) | C | Decided |
| D8 | Test framework / package manager | GoogleTest via FetchContent vs vcpkg | GoogleTest `v1.15.2` via CMake `FetchContent` | C | Decided |
| D9 | Health's causal role | Caps max tier vs defer entirely | Health drives biological collapse (`Dormant`) in `BehaviourFSM` | B | Decided |
| D10 | Simulation threading | Worker thread vs QTimer on a worker | Dedicated `std::thread` in `SimulationThread` | C | Decided |
| D11 | Config format for thresholds/tiers | JSON vs TOML vs QML-independent C++ struct + file | C++ config structs (`BehaviourFSMConfig`, `MetabolismConfig`, `LocomotionConfig`) with compiled defaults | B / C | Decided (compiled defaults; external file deferred) |
| D12 | Logging approach | Qt logging vs small custom rate-limited logger | Qt `qInfo` / `qWarning` / `qCritical` | C | Open (minimal) |

## Decided

| # | Date | Decision | Rationale |
|---|---|---|---|
| 1 | 2026-11-15 | C++20 + Qt 6/QML + SQLite; Python only for offline experiments | From the project report; avoids browser runtime overhead |
| 2 | 2026-11-15 | Deterministic controller first; no ML in critical loop | Baseline must be validated first |
| 3 | 2026-11-15 | Observation / decision / execution separation | Core architecture principle |
| 4 | 2026-11-15 | Split work across three devs by module ownership | Enables parallel progress ([roadmap.md](roadmap.md)) |
| 5 | 2026-11-25 | Keep Mate-Engine–inspired animations, character presets, and `LocomotionEngine` strictly in the UI layer (`src/ui/`, `resources/qml/`) | Preserves frozen contracts (`kContractVersion = 2`), keeps Brain/Metabolism/Memory untouched, stores UI-only preferences (`characterId`, `locomotionEnabled`) in `QSettings`, and gates motion on `QualityTier` and `Dormant` |

## Entry template

```
### DN: <title>
Date:
Context:
Options considered:
Decision:
Consequences / follow-ups:
```
