# Animation System Specification

**Module:** UI (`src/ui/`, `resources/qml/`)  
**Owner:** Developer C  
**Status:** Implemented (Phase 1–5)  
**Inspiration:** Mate-Engine (`shinyflvre/Mate-Engine`) — `AvatarAnimatorController.cs`, `AvatarLocomotionController.cs`, `AvatarSwayController.cs`, `AvatarMouseTracking.cs`, `AvatarHideHandler.cs`, `AvatarTaskbarController.cs`

---

## Overview

PROMETHEUS adds a Mate-Engine-inspired animation state machine layered entirely in the
view tier. No simulation contracts (`OrganismState`, `ComputeBudget`, `OrganismSnapshot`)
were modified. All animation logic is derived from values already present in
`OrganismSnapshot` and expressed in two new ViewModel properties plus a dedicated QML
component (`AnimationLayer.qml`).

---

## 1. `animState` Property

**Source:** `OrganismViewModel::animState()` (view-layer derived, no contract change)  
**Type:** `QString`  
**Updated:** Every `snapshotChanged()` signal (each sim tick)

Maps `Behaviour × QualityTier` → a single animation key consumed by `AnimationLayer.qml`.

| `Behaviour`   | `QualityTier` | `animState`         | Description |
|---|---|---|---|
| `Dormant`     | any           | `"dormant"`         | Minimal state, greyscale breathing |
| `Resting`     | 0–1           | `"rest_deep"`       | Collapsed rest, slow recovery |
| `Resting`     | 2–4           | `"rest_light"`      | Light rest, slight slouch |
| `Idle`        | any           | `"idle"`            | Baseline equilibrium |
| `Observing`   | any           | `"alert"`           | Upright, attentive posture |
| `Exploring`   | 3–4           | `"explore_full"`    | Full-vitality dance pulse |
| `Exploring`   | 0–2           | `"explore_lite"`    | Budget-capped light motion |
| `Stressed`    | 0–1           | `"stressed_critical"` | Hunched tremor, critical |
| `Stressed`    | 2–4           | `"stressed_mild"`   | Mild defensive posture |

### Body Transform Targets per `animState`

| `animState`         | Scale  | Rotation | Y-Offset |
|---|---|---|---|
| `dormant`           | 0.88   | 0°        | +4 px    |
| `rest_deep`         | 0.92   | −3°       | +3 px    |
| `rest_light`        | 0.96   | −1.5°     | +1.5 px  |
| `idle`              | 1.00   | 0°        | 0 px     |
| `alert`             | 1.03   | +2°       | −2 px    |
| `explore_full`      | 1.07   | 0°        | −4 px    |
| `explore_lite`      | 1.04   | 0°        | −2 px    |
| `stressed_critical` | 0.94   | −4°       | +2 px    |
| `stressed_mild`     | 0.97   | −2°       | +1 px    |
| *(dragging)*        | 1.08   | +7°       | −6 px    |

---

## 2. `animPulse` Property

**Source:** `OrganismViewModel::animPulse()` (view-layer derived)  
**Type:** `float` ∈ [0.0, 1.0]  
**Formula:** `clamp(max(max(0, current − prev) × 5.0, max(0, current − trailingBaseline) × 4.0), 0.0, 1.0)` (suppressed to `0.0` on initial snapshot and when dormant)  
**Mate-Engine analog:** Audio BPM peak detection → dance burst trigger

`animPulse` captures upward spikes in host pressure — both immediate step jumps and
rapid ramps smoothed by the 1.0 s EMA filter at 30 Hz. When `animPulse > 0.3` (and not
dormant), `AnimationLayer` fires a scale-burst animation (flinch: up to +12% scale over
70 ms, then 350 ms decay back to baseline) and `pulseRing` flares its border width. This
gives the organism a startled reaction to sudden host load increases — causal, testable,
and isolated to the view layer.

---

## 3. Animation Phases

### Phase 1 — Behaviour State Machine (`AnimationLayer.qml`)

**Mate-Engine analog:** `AvatarAnimatorController.cs` FSM parameter → animator state

Each `animState` value maps to body transform targets (`targetScale`, `targetRotation`,
`targetYOffset`). Crossfading `Behavior {}` blocks on internal `_baseScale`,
`_baseRotation`, and `_baseYOffset` properties blend smoothly between states before
additive offsets are applied to `bodyScale`, `bodyRotation`, and `bodyYOffset` (applied
to `catBody` via `scale`, `rotation`, and `transform: Translate`).

**Crossfade duration by `QualityTier` / state** (Mate-Engine: `AnimationTransitionTime`):

| Condition | Duration |
|---|---|
| Tier 0 (`Dormant`) | 0 ms (instant, `Behavior` disabled) |
| Dragging (awake)   | 120 ms |
| Tier 1 (`Critical`) | 150 ms |
| Tier 2 (`Constrained`) | 300 ms |
| Tier 3 (`Nominal`) | 500 ms |
| Tier 4 (`Optimal`) | 700 ms |

### Phase 2 — Pressure-Reactive Flinch

When `vm.animPulse > 0.3` and organism is not dormant:
- Scale burst: +(`animPulse × 0.12`) over 70 ms, decays in 350 ms (latches peak until cycle finishes or a higher spike arrives)
- Stress ring flare: border width swells up to +(`animPulse × 8` px) over 80 ms, decays in 400 ms (when `vm.stress > 0.10`)

### Phase 2b — Stress Tremor

When `vm.stress > 0.75`, `vm.qualityTier >= 2`, not dormant, not dragging:
- High-frequency rotation micro-jitter (±1.5° / ±0.8° at 60 ms steps with 400 ms pause)
- Automatically resets to `0°` when stopped

### Phase 3 — Exploration Dance Pulse

When `vm.animState === "explore_full"`, not dormant, not dragging:
- Rhythmic scale breathe: +0.04 / −0.02 / +0.02 / 0.00 over ~1.55 s loop
- Mate-Engine analog: dance animation looping during audio-reactive mode

### Phase 4 — Idle Sub-Cycle

When `vm.animState === "idle"`, `vm.qualityTier >= 2`, not dormant, not dragging:
- Randomised pause [2000–6000 ms] between micro-animations (Mate-Engine: `AnimationChangeTime`)
- Sub-state A: gentle float-up (`−2.5 px`) + stretch scale swell (`+0.04`)
- Sub-state B: subtle head-tilt (`0° → +3° → −2° → 0°`) while settling back to baseline
- Re-rolls random pause after each cycle and resets offsets cleanly when interrupted

---

## 4. Drag Interaction

**Mate-Engine analog:** Drag & drop cursor state → floating avatar animation

Tracked as `isDragging: bool` in `Main.qml` (pure QML, no C++), cleared on `onReleased`
and `onCanceled`. When dragging (and awake):
- Scale → 1.08
- Rotation → +7°
- Y-offset → −6 px
- Transition → 120 ms crossfade
- Particle orbit smoothly reverses direction from its current angle via `FrameAnimation`

---

## 5. Companion Character Selector (`CharacterPanel.qml`)

**Mate-Engine analog:** Built-in / custom avatar selection while preserving the full
animation pipeline.

- **Built-in presets (`OrganismViewModel::availableCharacters`):**
  - `cat` — **Ember Cat** (`qrc:/Prometheus/resources/organism.svg`, default)
  - `owl` — **Aegis Owl** (`qrc:/Prometheus/resources/characters/owl.svg`)
  - `fox` — **Spark Fox** (`qrc:/Prometheus/resources/characters/fox.svg`)
  - `bot` — **Atlas Bot** (`qrc:/Prometheus/resources/characters/bot.svg`)
  - `slime` — **Nova Slime** (`qrc:/Prometheus/resources/characters/slime.svg`)
- **Custom local asset:** Drag-and-drop a local `.svg`, `.png`, `.jpg`, `.jpeg`, `.webp`,
  `.gif`, or `.bmp` file onto `CharacterPanel.qml` (`selectCustomCharacter`). Remote URLs
  are rejected; missing custom files fall back to `"cat"` on startup.
- **Persistence:** Saved via `QSettings` (`ui/characterId`, `ui/customCharacterPath`) in
  the UI layer only — `MemoryManager` SQLite schema and simulation contracts are untouched.
- **UX:** Toggle with **`C`** or the hover **`Chars [C]`** pill (or `"Switch Character [C]"`
  in the diagnostics overlay). Opening the panel expands the window horizontally so the
  live organism remains visible alongside the character cards.

---

## 6. Phase 5 — Moving Animations & Desktop Locomotion (`LocomotionEngine`)

Direct feature comparison against `shinyflvre/Mate-Engine` and how PROMETHEUS adapts each moving-animation system to stay budget-coupled and deterministic:

| Mate-Engine Script | Mate-Engine Behaviour | PROMETHEUS Implementation |
|---|---|---|
| `AvatarLocomotionController.cs` | `SetWindowPos` horizontal patrols (`WalkLeft` / `WalkRight`), edge turnaround, random dwell timer | `LocomotionEngine` (`IdleWander` at Tier >= 2, `ExplorePatrol` scaled by `QualityTier` & `curiosity`), sub-pixel accumulator, screen-edge reversal, paused on hover/open panel/dormancy |
| `Walk` `.anim` clips | Skeletal walk cycle left/right | `AnimationLayer.qml`: `walkPhase`-driven stride hop (`\|sin\|`), forward lean + waddle, bottom-anchored squash-and-stretch (`Scale`), smooth horizontal flip (`facingScaleX`), per-character personality (`cat`, `owl`, `fox`, `bot`, `slime`), dynamic ground shadow & trailing particle wake |
| `AvatarSwayController.cs` | Damped harmonic spring pendulum driven by window drag velocity | `LocomotionEngine::updateSwaySpring()`: 2.6 Hz damped spring (`swayRotation`, `swayOffsetX`, `swayOffsetY`) during drag & release settle (Tier >= 2; exponential clamp at Tier 1) |
| `AvatarMouseTracking.cs` | Head/spine/eyes smoothly rotate toward cursor | `LocomotionEngine` cursor gaze in `Observing` / `Idle` (Tier >= 2): clamped `gazeLean`, `gazeShiftX`, and cursor-facing flip in `Observing` |
| `AvatarHideHandler.cs` | Snap/unsnap hysteresis near monitor left/right edges (`HideLeft` / `HideRight`) | `edgePeekSide` (`-1` / `+1`) snap (`<= 14 px`) and unsnap (`> 34 px`), tucks `34 px` past monitor edge with inward peek tilt (`±14°`) |
| `AvatarTaskbarController.cs` | Sit on top of Windows taskbar (`isSitting`) | Detects `Screen.desktopAvailableTop + Screen.desktopAvailableHeight` work-area bottom (`isSitting`) in `Idle`/`Resting`; stands up automatically when `Exploring` patrols |

### Budget & Dormancy Guarantees for Moving Animations

- **Dormant / Tier 0:** Immediately halts walking, clears spring sway and gaze, disables `FrameAnimation` and the `250 ms` decision timer.
- **Stressed / Resting:** Autonomous desktop walking and cursor gaze are suppressed so the organism stays still and conserves host resources.
- **Render-budget pacing:** `Main.qml` throttles `locomotionFrameAnim` steps to `vm.targetRenderHz` (`ComputeBudget::target_render_hz`) when not actively dragged, and drops back to a `4 Hz` decision timer while stationary between strolls.
- **Controls:** Toggle autonomous walking anytime with **`M`** or the hover **`Move [M]`** pill (persisted in `QSettings` as `ui/locomotionEnabled`).

---

## 7. Files Changed

| File | Change |
|---|---|
| `include/prometheus/ui/LocomotionEngine.hpp` | **New** — pure C++20 deterministic locomotion, spring sway, gaze, edge-peek, and taskbar-sit engine |
| `src/ui/LocomotionEngine.cpp` | **New** — implementation of `LocomotionEngine` |
| `include/prometheus/ui/OrganismViewModel.hpp` | Added `animState`, `animPulse`, `targetRenderHz`, locomotion Q_PROPERTYs / slots, and character selection Q_PROPERTYs |
| `src/ui/OrganismViewModel.cpp` | Implemented `computeAnimState()`, `computeAnimPulse()`, `stepLocomotion()`, character presets, custom file validation, and `QSettings` persistence |
| `resources/qml/AnimationLayer.qml` | **New** — full animation & procedural gait state machine component |
| `resources/qml/CharacterPanel.qml` | **New** — companion character picker panel with live previews and file drop zone |
| `resources/qml/Main.qml` | Integrated `AnimationLayer`, `CharacterPanel`, locomotion loop, ground shadow, `[M]` toggle, drag spring sway, stress flare, `FrameAnimation` orbit, diag panel update |
| `resources/characters/{owl,fox,bot,slime}.svg` | **New** — built-in vector character assets |
| `CMakeLists.txt` | Registered `LocomotionEngine`, `AnimationLayer.qml`, `CharacterPanel.qml`, and character SVGs |
| `tests/ui/test_locomotion_engine.cpp` | **New** — GoogleTest unit tests for locomotion determinism, Tier gating, edge turnaround, spring sway, edge-peek, taskbar-sit, and gaze |
| `tests/ui/test_organism_viewmodel.cpp` | **New** — GoogleTest unit tests for `animState`, `animPulse`, locomotion integration, and character selection/persistence |
| `tests/CMakeLists.txt` | Added `prometheus_ui_tests` target |
| `prometheus-docs/animation-spec.md` | **This file** |

---

## 8. NORTHSTAR Compliance

| Rule | Status |
|---|---|
| No decorative biology | Every animation state has a causal trigger from `Behaviour`, `QualityTier`, or pressure |
| Bounded | All durations and pulse values are finite and clamped; no dynamic allocation in hot path |
| Budget-driven | Crossfade duration and micro-animations gated on `qualityTier` |
| Dormancy respected | `animPulse` suppressed, `Behavior` crossfades disabled, and non-dormant loops halted when dormant |
| Deterministic | Driven by snapshot values; idle sub-cycle uses bounded `Math.random()` in view only |
| No contract change | `animState` / `animPulse` are ViewModel-layer derived; `OrganismSnapshot` unchanged |
| No new dependency | Pure QML `QtQuick` primitives + `Qt6::Test` in test target only |
