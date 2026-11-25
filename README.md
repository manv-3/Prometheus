# PROMETHEUS

> *An artificial organism that learns to survive within the computational limitations of its host.*

PROMETHEUS is a Windows desktop artificial-life organism built with **C++20**, **CMake**, **Qt 6 / QML**, and **SQLite**. A transparent, interactive companion lives on your desktop, senses host CPU and RAM conditions (or scripted synthetic telemetry curves), maintains internal biological drives (`energy`, `stress`, `health`, `curiosity`, `adaptation`), selects a deterministic `Behaviour`, converts that into a hard-capped `ComputeBudget`, and **adapts its own computational and visual workload** in real time—entering genuine low-cost dormancy under severe pressure and restoring its identity from SQLite when conditions recover.

Built for the Kiroween **Frankenstein** theme: sensory monitors, brain, metabolism, bounded particle body, SQLite memory, and an expressive animated desktop shell stitched into one coherent organism.

---

## Status

> **Core MVP + Mate-Engine–inspired Animation & Locomotion (Phases 1–5) implemented.**
> Process-level benchmark numbers (~50 MB idle RAM / ~1% avg CPU) remain **aspirational targets** until measured with `scripts/benchmark.ps1` on a documented machine. Official Kiroween rules remain **unverified** (see [prometheus-docs/hackathon-checklist.md](prometheus-docs/hackathon-checklist.md)).

| Area | Status | Notes |
|---|---|---|
| Shared contracts (`include/prometheus/common/`) | Implemented (`kContractVersion = 2`) | Frozen value-type structs (`HostObservation`, `OrganismState`, `ComputeBudget`, `OrganismSnapshot`, `Behaviour`, `QualityTier`) |
| Sensory (`src/sensory/`, `src/platform/windows/`) | Implemented | Real Win32 PDH CPU + `GlobalMemoryStatusEx` RAM (`valid=false` on missing/first read), `SensorWorker`, `SyntheticProvider` |
| Brain (`src/brain/`) | Implemented | Asymmetric `EmaFilter`, `PressureEstimator`, `HomeostasisEngine`, deterministic `BehaviourFSM` |
| Metabolism (`src/metabolism/`) | Implemented | Asymmetric degrade/recover `QualityTier` (0–4), `--fixed-tier N` benchmark mode, unconditional `HardLimits` clamp |
| Body (`src/body/`) | Implemented | Fixed-capacity `ParticleSystem` (`HARD_MAX_PARTICLES = 1000`, O(1) swap-with-last, zero hot-path alloc) |
| Memory (`src/memory/`) | Implemented | Versioned SQLite identity & checkpoints (`prometheus.db`), atomic transitions, NaN/Inf sanitization |
| Runtime & UI (`src/core/`, `src/ui/`, `resources/qml/`) | Implemented | `SimulationThread`, `OrganismViewModel`, `LocomotionEngine`, `Main.qml`, `AnimationLayer.qml`, `CharacterPanel.qml` |
| Validation & Demo Tooling (`scripts/`, `tests/`) | Implemented | 145 passing unit tests (`ctest`); `scripts/benchmark.ps1` & `scripts/demo-stress.ps1` ready (formal runs TBD) |

---

## The one proof

Under rising host pressure, PROMETHEUS:

1. **reduces its own computational work** (lowering `target_sim_hz`, `target_render_hz`, `max_particles`, and animation complexity),
2. **preserves its identity** across dormancy and restarts via SQLite checkpoints, and
3. **resumes normal activity** via staged recovery when host conditions stabilize.

See [NORTHSTAR.md](NORTHSTAR.md) for the full thesis and decision filter.

## What it is / is not

| It is | It is not |
|---|---|
| A resource-aware artificial-life runtime with an animated desktop body | A pure decorative skin disconnected from host budget |
| A feedback loop: sense → decide → budget → act | A passive CPU/RAM dashboard |
| Deterministic by default, bounded, and unit-tested | An LLM chatbot or ML loop in the critical path |
| Expressive while obeying `ComputeBudget` and true `Dormant` stops | A claim of biological life or consciousness |

---

## Architecture at a glance

```text
 Host (Windows PDH / RAM) ──► Sensory ──► Brain ──► Metabolism ──► Body ──► OrganismSnapshot ──► Qt/QML UI
                                 ▲          │            │                        │                 │
                                 │          ▼            ▼                        ▼                 ▼
                            Synthetic   Memory (SQLite): identity,         LocomotionEngine   AnimationLayer
                            Telemetry   events, checkpoints                (view-layer only)  CharacterPanel
```

- **Sensory (`src/sensory/`, `src/platform/windows/`):** collects and normalizes host observations with explicit `valid` flags and `steady_clock` timestamps.
- **Brain (`src/brain/`):** pure C++20 `PressureEstimator`, `HomeostasisEngine`, and `BehaviourFSM` (`Dormant`, `Resting`, `Idle`, `Observing`, `Exploring`, `Stressed`).
- **Metabolism (`src/metabolism/`):** maps `(Behaviour, smoothedPressure)` to `QualityTier` (0–4) and clamps `ComputeBudget` to `HardLimits`.
- **Body (`src/body/`):** runs a fixed-capacity particle pool inside `budget.max_particles`.
- **Memory (`src/memory/`):** persists organism identity, events, and checkpoints to SQLite on transitions, periodically, and on shutdown.
- **Core (`src/core/`):** `RuntimeCoordinator`, `RuntimeScheduler`, and `SimulationThread` publish immutable `OrganismSnapshot` instances via queued signals.
- **UI (`src/ui/`, `resources/qml/`):** `OrganismViewModel`, `LocomotionEngine`, `AnimationLayer.qml`, `CharacterPanel.qml`, and `Main.qml` render snapshots and view-layer animations without touching simulation internals.

---

## Desktop Animations, Characters & Controls

Inspired by [`shinyflvre/Mate-Engine`](https://github.com/shinyflvre/Mate-Engine) (`AvatarAnimatorController`, `AvatarLocomotionController`, `AvatarSwayController`, `AvatarMouseTracking`, `AvatarHideHandler`, `AvatarTaskbarController`), PROMETHEUS couples interactive desktop animations directly to `Behaviour` and `QualityTier` (documented in [prometheus-docs/animation-spec.md](prometheus-docs/animation-spec.md)):

- **State-driven postures & reactions:** `animState` (`Behaviour × QualityTier`), `animPulse` pressure-spike flinch, exploration dance pulse (`explore_full`), stress tremor, idle micro-animations, and greyscale `Dormant` breathing.
- **Autonomous desktop walking (`LocomotionEngine`):** patrols across the desktop work area (`IdleWander` at Tier ≥ 2, `ExplorePatrol` scaled by `QualityTier` and `curiosity`), reverses at monitor borders, pauses on pointer hover or open panels, and halts in `Dormant`, `Stressed`, and `Resting`.
- **Moving gait & character personalities:** procedural stride bounce, forward lean, waddle, bottom-anchored squash-and-stretch, directional left/right flip, ground contact shadow, and trailing particle wake tailored per built-in character (**Ember Cat**, **Aegis Owl**, **Spark Fox**, **Atlas Bot**, **Nova Slime**) or custom dropped `.svg`/`.png`/`.jpg`/`.webp` assets.
- **Desktop interactions:** damped spring-pendulum drag sway, monitor-edge peek snap/unsnap (`edgePeekSide`), taskbar-edge sitting (`isSitting`), and cursor gaze tracking in `Observing` / `Idle`.

### Keyboard & Mouse Controls

| Input | Action |
|---|---|
| **Left-drag** | Reposition organism anywhere on screen (with spring-damped inertia sway, edge-peek near left/right monitor borders, and taskbar sitting near the bottom edge) |
| **`C`** or hover **`Chars [C]`** pill | Open/close **Character Selector** (`CharacterPanel.qml`) or drop a custom local image |
| **`M`** or hover **`Move [M]`** pill | Toggle autonomous desktop walking on/off (persisted via `QSettings`) |
| **`D`** or **Right-click** | Toggle **Developer Diagnostics Panel** (Organism drives, Animation/Locomotion state, Host telemetry, Runtime Hz, `LIVE`/`SYNTHETIC` badge) |
| **`Esc`** | Close open overlay panels |

---

## Building and Running

Prerequisites and setup details: [DEVCONTEXT.md](DEVCONTEXT.md).

### Qt 6 MinGW Kit (configured preset)

```powershell
# Configure, build, and run tests
cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug
ctest --preset windows-mingw-debug --output-on-failure

# Launch interactive desktop organism (or double-click run.bat)
.\run.bat --synthetic
```

### MSVC Kit

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

### Runtime Flags & Validation Scripts

- `--synthetic` — use scripted `SyntheticProvider` telemetry (displays `SYNTHETIC` badge in UI).
- `--fixed-tier <0..4>` — pin `Metabolism` to a constant `QualityTier` for fixed-complexity benchmark baselines.
- `.\scripts\benchmark.ps1` — process CPU / working-set harness comparing adaptive vs. fixed-complexity runs (`REQ-BENCH-01`).
- `.\scripts\demo-stress.ps1` — structured Survival Test demonstration script using bounded `scripts\stress-worker.ps1`.

---

## Repository Layout

```text
Prometheus/
├── CMakeLists.txt  CMakePresets.json  run.bat
├── README.md  NORTHSTAR.md  AGENTCONTEXT.md  DEVCONTEXT.md  CONTRIBUTING.md
├── .kiro/                  # Kiro steering, specs, and workflow hooks
├── prometheus-docs/        # Architecture, contracts, model, animation spec, methodology, roadmap
├── include/prometheus/     # Public module headers (common/, brain/, metabolism/, body/, sensory/, memory/, core/, ui/, platform/windows/)
├── src/                    # C++20 implementations matching include/prometheus/ + app/main.cpp
├── resources/              # QML views (Main.qml, AnimationLayer.qml, CharacterPanel.qml) and character SVGs
├── scripts/                # PowerShell benchmark & Survival Test stress scripts
└── tests/                  # GoogleTest suites (brain, body, metabolism, memory, ui)
```

---

## Documentation Index

| Document | Purpose |
|---|---|
| [NORTHSTAR.md](NORTHSTAR.md) | Thesis, success criteria, non-goals, decision filter |
| [AGENTCONTEXT.md](AGENTCONTEXT.md) | Rules and context for AI coding agents |
| [DEVCONTEXT.md](DEVCONTEXT.md) | Onboarding, setup, workflow, roles, status board |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Branching, commits, PR checklist |
| [prometheus-docs/architecture.md](prometheus-docs/architecture.md) | Subsystems, threading, update loop, dormancy, persistence |
| [prometheus-docs/interfaces.md](prometheus-docs/interfaces.md) | Shared data contracts and change-control protocol |
| [prometheus-docs/organism-model.md](prometheus-docs/organism-model.md) | Variables, equations, state machine, adaptation vs evolution |
| [prometheus-docs/animation-spec.md](prometheus-docs/animation-spec.md) | Mate-Engine–inspired animation state machine, locomotion, and character selector |
| [prometheus-docs/benchmark-methodology.md](prometheus-docs/benchmark-methodology.md) | How process-level performance claims are measured |
| [prometheus-docs/testing-strategy.md](prometheus-docs/testing-strategy.md) | Unit, integration, fault-injection, CI |
| [prometheus-docs/roadmap.md](prometheus-docs/roadmap.md) | Phases, 3-developer split, progress, exit criteria |
| [prometheus-docs/demo-script.md](prometheus-docs/demo-script.md) | The "Survival Test" demonstration |
| [prometheus-docs/hackathon-checklist.md](prometheus-docs/hackathon-checklist.md) | Unverified event requirements to confirm |
| [prometheus-docs/risks.md](prometheus-docs/risks.md) | Risk register |
| [prometheus-docs/decisions.md](prometheus-docs/decisions.md) | Decision log and ADRs |
| [prometheus-docs/glossary.md](prometheus-docs/glossary.md) | Terminology |

---

## Team

| Role | Owns |
|---|---|
| Dev A: Senses & Memory | `sensory/`, `platform/windows/`, `memory/`, benchmark tooling |
| Dev B: Brain & Metabolism | `brain/`, `metabolism/` |
| Dev C: Body, UI & Runtime (integration lead) | `app/`, `core/`, `body/`, `ui/`, CMake/Qt setup |

## License

To be decided before first public release.
