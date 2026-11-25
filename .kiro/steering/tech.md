# PROMETHEUS – Technical Steering

## Stack

| Layer | Technology |
|---|---|
| Language | C++20 (`CMAKE_CXX_STANDARD 20` — MinGW GCC 13 active preset or MSVC) |
| Build | CMake 3.20+ with `CMakePresets.json`; Ninja generator |
| UI | Qt 6 (6.12.0 MinGW kit configured) — Qt Quick / QML / Svg; thin `OrganismViewModel` + `LocomotionEngine` |
| Persistence | SQLite3 (`MemoryManager` for organism identity/checkpoints; `QSettings` for UI-only skin & locomotion prefs) |
| Testing | GoogleTest `v1.15.2` (via CMake `FetchContent`) + `Qt6::Test` for UI tests |
| Platform APIs | Windows PDH (CPU), `GlobalMemoryStatusEx` (RAM); optional NVML deferred |
| Optional research | Python 3 in `experiments/` only — never in the core build |

## Module dependency rule

```text
app/ui  →  core  →  {sensory, brain, metabolism, body, memory}
```

`brain/` and `metabolism/` depend **only** on the C++ standard library and shared types (`include/prometheus/common/`). No Qt headers, no Windows headers, no PDH in `brain/` or `metabolism/`. Platform code belongs exclusively in `src/platform/windows/` behind `SensoryProvider`.

`LocomotionEngine` (`include/prometheus/ui/LocomotionEngine.hpp`) is pure C++20 view-layer presentation logic (no Win32, DB, or simulation mutations); `OrganismViewModel` exposes its outputs to QML.

## Code conventions

- **Types:** `PascalCase`. **Functions/members:** `camelCase`. **Constants:** `kPascalCase`. **Files:** match the primary type name (`.hpp` / `.cpp`).
- **Time:** `std::chrono::steady_clock` for all timestamps and intervals. `float` seconds for `deltaTime`. Never wall-clock time for control logic.
- **Errors:** no exceptions across module boundaries in the hot loop. Return explicit result types or validity flags (`cpu_valid`, `memory_valid`). Missing telemetry is **never** substituted with zero.
- **Config over magic numbers:** every threshold, weight, tier limit, locomotion parameter, and hard cap lives in a named config struct (`BehaviourFSMConfig`, `MetabolismConfig`, `LocomotionConfig`) with documented defaults.
- **Logging:** rate-limited. Sensor failures must not flood the log.
- **Comments:** explain *why*, especially for thresholds and hysteresis. Mark untuned constants `// initial, to be tuned`.
- **Warnings:** `/W4` on MSVC, `-Wall -Wextra -Wpedantic` on GCC/MinGW (target `/WX` when clean).

## Shared contracts (frozen, `kContractVersion = 2`)

The following types live in `include/prometheus/common/` and require both other devs' approval to change:  
`HostObservation`, `OrganismState`, `Behaviour`, `ComputeBudget`, `QualityTier`, `OrganismSnapshot`, `TelemetrySource`, `kContractVersion`.

See `prometheus-docs/interfaces.md` for change-control rules.

## Threading model

| Thread | Owns |
|---|---|
| GUI thread | Qt objects, QML scene (`Main.qml`, `AnimationLayer.qml`, `CharacterPanel.qml`), `OrganismViewModel`, `LocomotionEngine` — never blocks on I/O, DB, or simulation |
| Sensor worker | `SensorWorker` sampling loop; mutex-protected latest `HostObservation` |
| Simulation thread | `SimulationThread` drives Brain, Metabolism, Body, and synchronous SQLite `MemoryManager` checkpoints; publishes immutable `OrganismSnapshot` |

Snapshots cross threads via `std::shared_ptr<const OrganismSnapshot>` and `Qt::QueuedConnection`. Live simulation objects never cross thread boundaries.

## Hard rules (never violate)

1. `ComputeBudget` never exceeds `HardLimits` (`HARD_MAX_PARTICLES = 1000`, `HARD_MAX_SIM_HZ = 60`, `HARD_MAX_RENDER_HZ = 60`).
2. Dormancy stops normal simulation updates (`SimulationThread` parks on 0.2 Hz heartbeat) and halts UI animations/locomotion timers. Hiding the window is not dormancy.
3. Rendering never triggers a Brain or simulation update.
4. Brain and Metabolism are pure: same inputs + same `deltaTime` = same output. `LocomotionEngine` uses an explicit seeded `std::mt19937` for reproducible tests.
5. No unbounded containers in hot paths: fixed-capacity `ParticleSystem` (`std::array`), zero per-frame allocations in UI `stepLocomotion()`.
6. No new external dependencies without a decision logged in `prometheus-docs/decisions.md`.
7. Never fabricate benchmark numbers, RAM/CPU figures, or test outcomes. Synthetic telemetry (`--synthetic`) is always labelled as synthetic.

## Build and test commands

```powershell
# Active MinGW + Qt 6.12 preset
cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug
ctest --preset windows-mingw-debug --output-on-failure
.\run.bat --synthetic

# MSVC preset (if Qt MSVC kit is installed)
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```
