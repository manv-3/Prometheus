# PROMETHEUS – Product Steering

## What this is

PROMETHEUS is a Windows desktop artificial-life experiment built for the Kiroween 2026 hackathon (**Frankenstein** track). A transparent, frameless organism lives on the desktop, observes real host conditions (CPU, RAM, optionally GPU), maintains internal state (energy, stress, curiosity, adaptation), and **changes its own computational footprint** in response: it simplifies its simulation and rendering under pressure, goes dormant when it must, and wakes with its identity intact when conditions recover.

**Frankenstein framing:** a body, senses, brain, metabolism and memory — each built separately, stitched into one coherent entity that is more than the sum of its parts.

## The one proof

Under rising host pressure the organism:
1. **reduces its own measured computational work** (process CPU and private working set, verifiable with Process Explorer or WPR),
2. **preserves its identity** (same persistent ID, age, essential state across dormancy and restart),
3. **resumes normal activity when conditions recover**.

Every feature exists to support this proof. If a piece of work does not strengthen it, it is not MVP.

## Zero-requirement (the thing that must never break)

> The organism must never increase its computational footprint in response to rising host pressure.

This is the invariant that justifies the project's existence. Any code path that might violate it blocks a merge.

## Target users

- Developers and power users who want an interactive artefact that visibly respects the host machine.
- Hackathon judges evaluating the Frankenstein category: distinct subsystems (senses, brain, metabolism, body, memory) stitched into one functioning entity.

## Tone and vocabulary

Use the project's established terminology exactly (see `prometheus-docs/glossary.md` and `include/prometheus/common/*.hpp`): `HostObservation`, `OrganismState`, `ComputeBudget`, `QualityTier`, `Behaviour`, `OrganismSnapshot`, `smoothed_pressure` / `environmental_pressure`.  
Do not coin synonyms. Precision matters because human reviewers and AI agents share the same vocabulary.

## Companion UI & moving animations (view-layer only)

PROMETHEUS includes an expressive Qt/QML desktop surface (`AnimationLayer.qml`, `CharacterPanel.qml`, `LocomotionEngine`) inspired by `Mate-Engine`. All companion skins, autonomous desktop walking, spring-damped drag sway, cursor gaze, edge-peek, and taskbar sitting remain strictly view-layer (`src/ui/`, `resources/qml/`), throttled by `ComputeBudget::target_render_hz`, gated by `QualityTier`, and halted completely when `Dormant`.

## Scope limits (what this is not)

- Not a CPU/RAM dashboard.
- Not a decorative desktop pet disconnected from host `ComputeBudget`.
- Not an LLM chatbot or AI personality.
- Not a genetic algorithm or ML project.
- Not a multi-organism ecosystem (post-MVP).
- Not a claim of consciousness or biological life.

See `NORTHSTAR.md` for the full decision filter.
