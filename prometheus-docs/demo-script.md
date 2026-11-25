# Demo Script: The Survival Test

The demo must show a **causal chain** from host pressure → organism behaviour → measured application workload. A beautiful animation is not proof.

> Everything shown must be live or clearly labelled. Never present synthetic telemetry or prerecorded results as live measurements.

## Stages

| # | Stage | Action | Evidence on screen |
|---|---|---|---|
| 1 | **Birth** | Launch; initialize identity | Initial state, age, energy, behaviour |
| 2 | **Healthy environment** | Let it explore normally | Host telemetry, particle count, current budget/tier |
| 3 | **Environmental pressure** | Run controlled CPU/memory stress | Pressure trend, state transition |
| 4 | **Survival** | Observe reduced complexity; dormancy if thresholds crossed | Particle count, sim Hz, process CPU/memory |
| 5 | **Resurrection** | Stop the stress | Recovery transition, restored identity (same id, age continuity) |
| 6 | **Proof** | Compare fixed vs adaptive | Process-level benchmark and budget evidence |

## Observer / developer panel

Separate diagnostics panel (`[D]` or right-click in `Main.qml`) judges can open. Shows:

- **Simulated organism variables:** Character name, energy, health, stress, curiosity, adaptation, behaviour, quality tier
- **Animation & locomotion state:** `AnimState`, `Pulse`, `Dragging`, `XFade`, `Move` (`LocomotionMode`, sitting/peek flag, facing direction), `WalkCtrl` (`ON`/`OFF` + screen coordinates)
- **Measured host values:** CPU %, RAM %, pressure (with red invalid indicator when `valid == false`)
- **Measured self-cost:** active particles, simulation Hz, age, and **LIVE / SYNTHETIC** badge (plus top-right yellow `SYNTHETIC` badge in `--synthetic` mode)

Quick UX controls during live demo: `[C]` switches companion skin (`CharacterPanel.qml`), `[M]` toggles autonomous desktop walking (`LocomotionEngine`), `[D]` toggles diagnostics, and dragging demonstrates spring sway, screen-edge peek, and taskbar sitting.

## Reliability plan

- [x] Synthetic telemetry mode (`--synthetic`) so the demo never depends on destabilizing the host (labelled)
- [x] Controlled stress tool (`scripts/demo-stress.ps1` + `scripts/stress-worker.ps1`) with a **safe stop** and duration cap
- [x] Live process metrics (`scripts/demo-stress.ps1` and `scripts/benchmark.ps1` sample `prometheus_app` directly), not only scripted visuals
- [ ] Recorded backup demo in case hardware/display misbehaves (labelled prerecorded)
- [ ] Rehearsed on the actual presentation machine and display setup
- [ ] Fresh-database run and pre-seeded-database run both tested
- [ ] Fallback if the click-through/transparent window misbehaves on the projector

## Talk track (outline)

1. The question: what if a desktop app treated the host as its environment?
2. The loop: sense → decide → budget → act (one diagram, [architecture.md](architecture.md)).
3. Live run through stages 1–5, narrating the evidence panel.
4. Proof slide: fixed vs adaptive numbers from [benchmark-methodology.md](benchmark-methodology.md), with machine and method stated.
5. Honest limits: what is aspirational, what is deferred, what is not claimed.

## Do not

- Quote RAM/CPU numbers that were not measured on the demo machine.
- Imply that adaptation is "learning" without a measured improvement.
- Map the demo to judging criteria before the official criteria are verified ([hackathon-checklist.md](hackathon-checklist.md)).
