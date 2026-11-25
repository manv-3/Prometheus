# AGENTCONTEXT

Context and rules for **AI coding agents** (Claude Code, Kiro, Copilot, etc.) working in this repository. Humans should read [DEVCONTEXT.md](DEVCONTEXT.md). If your tool expects a specific filename (`CLAUDE.md`, `AGENTS.md`, a Kiro steering file), copy or link this file; keep one source of truth.

> Whether the hackathon requires specific Kiro artifacts is **unverified**. Do not invent such requirements. See [prometheus-docs/hackathon-checklist.md](prometheus-docs/hackathon-checklist.md).

## 1. What this project is (30 seconds)

PROMETHEUS is a Windows desktop artificial-life organism (C++20, Qt 6/QML). It senses host CPU/RAM, keeps internal state, picks a behaviour with a deterministic controller, converts that into a hard-capped `ComputeBudget`, and runs a bounded simulation inside the budget. It can go dormant and restore identity from SQLite.

**The one proof:** under rising host pressure it reduces its own measured computational work, keeps its identity, and recovers. Read [NORTHSTAR.md](NORTHSTAR.md) before proposing any feature.

**Current status:** MVP + Phase 5 UI/locomotion implemented (145 CTest tests passing; benchmark runs TBD). Always verify against [DEVCONTEXT.md](DEVCONTEXT.md) status board and `git log`.

## 2. Module map and ownership

| Module | Path | Responsibility | Must NOT |
|---|---|---|---|
| Sensory | `src/sensory/`, `src/platform/windows/` | Collect/normalize host observations with validity + timestamp | Make organism decisions; render |
| Brain | `src/brain/` | Update `OrganismState`, pressure estimate, choose `Behaviour` | Include Qt or Windows headers; poll hardware |
| Metabolism | `src/metabolism/` | `ComputeBudget`, quality tiers, hard limits, degrade/recover pacing | Own visuals or persistence |
| Body | `src/body/` | Bounded particle simulation within budget | Read telemetry or touch the DB |
| Memory | `src/memory/` | SQLite identity, events, checkpoints | Make real-time simulation decisions |
| Core | `src/core/` | Coordinator, scheduler, snapshot publication, shared types | Contain biological rules or Win32 calls |
| UI | `src/ui/`, `src/app/` | QML + thin ViewModel over immutable snapshots | Hold mutable access to engine internals |

Dependency direction: `app/ui → core → {sensory, brain, metabolism, body, memory}`. `brain/` and `metabolism/` depend only on the C++ standard library and shared types. Platform code lives only under `platform/windows/` behind a portable interface.

## 3. Hard rules (non-negotiable)

1. **Shared contracts are frozen.** `HostObservation`, `OrganismState`, `ComputeBudget`, `OrganismSnapshot`, `Behaviour`, `QualityTier` live in [prometheus-docs/interfaces.md](prometheus-docs/interfaces.md) and `include/prometheus/common/`. Do not change them unilaterally. If a change is necessary, stop and propose it (see the change protocol in that doc).
2. **Missing telemetry is never zero.** Use `Metric{value, valid}`. Renormalize pressure weights over valid signals. Stale data triggers conservative fallback.
3. **Bounded everything.** Fixed-capacity particle pool (activate/deactivate, no per-frame alloc), capped histories and queues. No unbounded containers in hot paths.
4. **Hard limits have final authority.** No heuristic, learned model or tuning parameter may exceed them.
5. **Rendering never drives simulation.** Separate clocks; the UI reads the latest immutable snapshot.
6. **Brain/Metabolism are pure and deterministic.** Same state + observations + `deltaTime` ⇒ same output. No hidden randomness; if randomness is needed, inject a seeded RNG.
7. **GUI thread stays free.** No telemetry, DB or simulation work on the Qt GUI thread. QObjects stay on their owning thread.
8. **Dormancy is real.** It stops normal simulation and expensive rendering. Hiding the window is not dormancy.
9. **No decorative biology.** Every internal variable must have a causal effect and a test.
10. **No ML in the critical loop.** Deterministic baseline first.
11. **Never fabricate results.** Do not invent benchmark numbers, RAM/CPU figures, test outcomes or hackathon requirements. Placeholders must be visibly marked `TBD`. Label synthetic telemetry as synthetic everywhere it appears.
12. **No new dependencies** (Boost, ML frameworks, network services) without a documented need in [prometheus-docs/decisions.md](prometheus-docs/decisions.md).

## 4. Conventions

- **Language:** C++20, CMake. Warnings as errors on new code (`/W4 /WX` on MSVC) once clean.
- **Time:** `std::chrono::steady_clock` for timestamps and intervals; `float` seconds for `deltaTime`. Never use wall-clock time for control logic.
- **Errors:** no exceptions across module boundaries in the hot loop; return values/validity flags. I/O (Memory) returns explicit result types. (Ratified; see [prometheus-docs/decisions.md](prometheus-docs/decisions.md).)
- **Naming:** types `PascalCase`, functions/members `camelCase`, constants `kPascalCase`, files match the primary type.
- **Config over magic numbers:** thresholds, weights, tier tables and limits live in a config structure with documented defaults, not inline literals.
- **Logging:** rate-limited. Sensor failures must not flood the log.
- **Comments:** explain *why*, especially for thresholds and hysteresis. Mark every tuned constant as "initial, to be tuned".

## 5. Working method for any task

1. Read [NORTHSTAR.md](NORTHSTAR.md), the relevant `prometheus-docs/*.md`, and the files you will touch.
2. State which module you are changing and confirm it matches the owner's scope. Stay inside it.
3. Write or update **tests first or alongside** the change (see [prometheus-docs/testing-strategy.md](prometheus-docs/testing-strategy.md)).
4. Keep changes small and focused; one concern per PR.
5. Build and run tests before declaring done. If you cannot run them (e.g., no Windows environment), say so explicitly; do not claim they pass.
6. Update docs when behaviour, contracts or thresholds change.

## 6. When to stop and ask a human

- A contract change seems required.
- Two docs disagree (see [prometheus-docs/decisions.md](prometheus-docs/decisions.md)).
- A task would add a dependency, a thread, a persistent format or a public-facing claim.
- You cannot verify something that the work depends on (hackathon rules, hardware behaviour).

## 7. Task playbooks

**Adding an internal variable**
1. Define its range and meaning in [prometheus-docs/organism-model.md](prometheus-docs/organism-model.md).
2. Name the behaviour or budget it causally affects.
3. Add a bounds test and a causal-effect test.
4. If it must be shared, follow the contract change protocol.

**Adding a quality tier / changing a budget value**
1. Edit the config table, not code branches.
2. Add or adjust a test proving the budget never exceeds hard limits.
3. Record the measurement that justifies the value, or mark it `TBD (untuned)`.

**Adding a sensor**
1. Implement behind the portable interface in `sensory/`; Win32 code only in `platform/windows/`.
2. Return `Metric` with validity; add stale and unavailable fault-injection tests.
3. Add a synthetic equivalent so the Brain tests need no real hardware.

**Touching persistence**
1. Version the schema; add a migration or recovery path.
2. Use transactions and atomic checkpoint replacement.
3. Keep DB work off the render path; test corrupt/incomplete state.

## 8. Definition of done (for any PR)

- [ ] Builds clean; tests pass (or limitation stated)
- [ ] New logic has unit tests; boundaries and determinism covered
- [ ] No contract change without approval
- [ ] No unbounded allocation in hot paths; limits respected
- [ ] No Qt/Win32 leakage into `brain/` or `metabolism/`
- [ ] Docs updated; no invented numbers or claims
