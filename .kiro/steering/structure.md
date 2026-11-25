# PROMETHEUS – Repository Structure Steering

## Directory layout

```text
D:\Prometheus\
├── .kiro/
│   ├── steering/           # Always-included agent/AI context
│   │   ├── product.md      # What PROMETHEUS is, the one proof, scope limits
│   │   ├── tech.md         # Stack, conventions, threading, hard rules
│   │   └── structure.md    # This file: layout and module map
│   ├── specs/
│   │   └── prometheus-mvp/ # Spec-driven development: requirements, design, tasks
│   │       ├── requirements.md
│   │       ├── design.md
│   │       └── tasks.md
│   └── hooks/
│       ├── contract-guard.json   # Warns when shared contract headers are edited
│       └── northstar-check.json  # Reminds agent to check NORTHSTAR before new work
├── CMakeLists.txt           # Top-level CMake 3.20+ build & GoogleTest FetchContent
├── CMakePresets.json        # windows-mingw-{debug,release} & windows-msvc-{debug,release}
├── run.bat                  # Launches MinGW debug build with Qt 6.12 runtime PATH
├── README.md
├── NORTHSTAR.md             # Thesis, success criteria, decision filter — settle arguments here
├── AGENTCONTEXT.md          # Rules for AI coding agents (source of truth; this .kiro mirrors it)
├── DEVCONTEXT.md            # Onboarding, setup, and status board for human developers
├── CONTRIBUTING.md          # Branching, commits, PR checklist
├── prometheus-docs/         # Design & specification documents
│   ├── animation-spec.md    # Mate-Engine-inspired QML animations, skins & locomotion
│   ├── architecture.md      # Subsystems, threading, update loop, dormancy, persistence
│   ├── interfaces.md        # Shared data contracts and change-control rules
│   ├── organism-model.md    # Variables, equations, state machine, adaptation
│   ├── benchmark-methodology.md
│   ├── testing-strategy.md
│   ├── roadmap.md           # Phases, 3-developer split, exit criteria
│   ├── demo-script.md       # The "Survival Test" demonstration
│   ├── hackathon-checklist.md
│   ├── risks.md
│   ├── decisions.md
│   └── glossary.md
├── include/prometheus/      # Public C++20 headers by module
│   ├── common/              # Frozen contracts: HostObservation, OrganismState, ComputeBudget, OrganismSnapshot
│   ├── core/                # RuntimeCoordinator, RuntimeScheduler, SimulationThread
│   ├── sensory/             # SensoryProvider, SensorWorker, SyntheticProvider, RealSensorProvider
│   ├── platform/windows/    # CpuMonitor, MemoryMonitor
│   ├── brain/               # EmaFilter, PressureEstimator, HomeostasisEngine, BehaviourFSM
│   ├── metabolism/          # Metabolism
│   ├── body/                # ParticleSystem
│   ├── memory/              # MemoryManager
│   └── ui/                  # OrganismViewModel, LocomotionEngine
├── src/                     # C++20 module implementations
│   ├── core/                # Coordinator, scheduler, snapshot publication
│   ├── sensory/             # HostObservation providers & worker
│   ├── platform/windows/    # Win32 PDH & GlobalMemoryStatusEx
│   ├── brain/               # OrganismState update, pressure estimate, Behaviour selection
│   ├── metabolism/          # ComputeBudget, QualityTier, hard limits, pacing
│   ├── body/                # Bounded particle simulation, runs within ComputeBudget
│   ├── memory/              # SQLite identity, events, checkpoints
│   ├── ui/                  # OrganismViewModel, LocomotionEngine (view-layer only)
│   └── app/                 # Entry point (main.cpp), --synthetic, --fixed-tier
├── resources/               # QML views (Main.qml, AnimationLayer.qml, CharacterPanel.qml), organism.svg & characters/*.svg
├── scripts/                 # PowerShell benchmark.ps1, demo-stress.ps1, stress-worker.ps1
└── tests/                   # GoogleTest suites (brain/, body/, metabolism/, memory/, ui/)
```

## Module ownership

| Module | Path(s) | Owner | Must NOT |
|---|---|---|---|
| Sensory | `src/sensory/`, `src/platform/windows/` | Dev A | Make decisions; render |
| Brain | `src/brain/` | Dev B | Include Qt or Windows headers; poll hardware |
| Metabolism | `src/metabolism/` | Dev B | Own visuals or persistence |
| Body | `src/body/` | Dev C | Read telemetry or touch DB |
| Memory | `src/memory/` | Dev A | Make real-time simulation decisions |
| Core | `src/core/` | Dev C | Contain biology rules or Win32 calls |
| UI | `src/ui/`, `src/app/`, `resources/` | Dev C | Hold mutable access to engine internals |

## Naming rules for new files

- Header files live under `include/prometheus/<module>/<Type>.hpp`; implementations under `src/<module>/<Type>.cpp`.
- Test files live under `tests/<module>/test_<snake_name>.cpp` and are registered in `tests/CMakeLists.txt`.
- Platform-specific files live exclusively under `src/platform/windows/` and `include/prometheus/platform/windows/`.
- No `Utils.h` catch-alls. Each file has one clearly named responsibility.

## What belongs in prometheus-docs/ vs .kiro/

`prometheus-docs/` contains **design documents** that all three developers own and edit collaboratively. They are the authoritative source for contracts, model, architecture, animations, and methodology.

`.kiro/steering/` contains **agent-facing summaries** that distil those documents into always-available context for AI tools. When the two diverge, `prometheus-docs/` is correct; update the steering file to match.

`.kiro/specs/` contains **implementation specs** (requirements → design → tasks) used to drive Kiro's spec-driven workflow. They reference `prometheus-docs/` documents without duplicating full design details.
