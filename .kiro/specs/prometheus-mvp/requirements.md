# PROMETHEUS MVP – Requirements

> Status: **MVP implemented (Phases 0–5)**.  
> All thresholds are initial placeholders (`// initial, to be tuned`) until benchmark-calibrated.  
> Note on ratified enum naming (`include/prometheus/common/OrganismState.hpp`): the original draft names `Conserve`/`Recover`/`Explore` map to `Stressed`/`Resting`/`Exploring`, plus `Observing`.  
> Requirements use EARS form: WHEN [condition] THE SYSTEM SHALL [behaviour].

---

## REQ-SENSE-01 · CPU observation

WHEN the sensor worker samples host CPU utilisation  
THE SYSTEM SHALL return a `Metric{value, valid=true}` with a value in `[0.0, 1.0]`  
AND the sample SHALL be derived from an interval between two readings (not a single snapshot).

WHEN the PDH counter is unavailable or the interval has not yet elapsed  
THE SYSTEM SHALL return `Metric{valid=false}`  
AND SHALL NOT substitute zero or any other value for the missing sample.

---

## REQ-SENSE-02 · RAM observation

WHEN the sensor worker samples available RAM via `GlobalMemoryStatusEx`  
THE SYSTEM SHALL return `Metric{ramUsageFraction, valid=true}` in `[0.0, 1.0]`  
AND `Metric{availableRamMB, valid=true}` as the absolute headroom.

WHEN the system call fails  
THE SYSTEM SHALL return both metrics with `valid=false`.

---

## REQ-SENSE-03 · Synthetic telemetry

WHEN the application is started with `--synthetic`  
THE SYSTEM SHALL substitute a scripted `SensoryProvider` that emits pre-defined pressure curves (ramp, spike, plateau, stale, drop-out)  
AND SHALL label every `HostObservation` with `source=TelemetrySource::Synthetic`  
AND SHALL propagate that label into every `OrganismSnapshot` published to the UI.

---

## REQ-SENSE-04 · Stale data

WHEN a `HostObservation` is older than the configured staleness threshold  
THE SYSTEM SHALL mark the affected metrics `valid=false`  
AND the Brain SHALL apply a conservative pressure fallback rather than using the stale value.

---

## REQ-BRAIN-01 · Deterministic state update

WHEN `Brain::update` is called with a given `(HostObservation, OrganismState, deltaTime)`  
THE SYSTEM SHALL produce the same `OrganismState` output on every call  
AND SHALL NOT read from global mutable state, wall-clock time, or any external resource.

---

## REQ-BRAIN-02 · Pressure estimation

WHEN computing smoothed pressure  
THE SYSTEM SHALL apply an EMA with a larger α for rising pressure and a smaller α for falling pressure (asymmetric response)  
AND SHALL renormalise pressure weights over valid signals only  
AND SHALL NEVER treat a missing (invalid) signal as zero pressure.

---

## REQ-BRAIN-03 · Behaviour state machine

WHEN energy falls below the critical-low threshold  
THE SYSTEM SHALL select `Behaviour::Dormant` regardless of pressure or curiosity.

WHEN smoothed pressure exceeds the enter-Conserve threshold and the organism is not Dormant  
THE SYSTEM SHALL select `Behaviour::Conserve`  
AND SHALL NOT exit Conserve until smoothed pressure has remained below the exit-Conserve threshold for at least the minimum dwell time.

WHEN energy is below the low-energy threshold and pressure permits  
THE SYSTEM SHALL select `Behaviour::Recover`.

WHEN curiosity is above the explore threshold AND the current `ComputeBudget` has headroom  
THE SYSTEM SHALL select `Behaviour::Explore`.

OTHERWISE  
THE SYSTEM SHALL select `Behaviour::Idle`.

---

## REQ-BRAIN-04 · Transition safeguards

WHEN a behaviour transition is proposed  
THE SYSTEM SHALL enforce a minimum dwell time before accepting the transition  
AND SHALL require the trigger condition to persist across at least two consecutive decision intervals (debounce).

---

## REQ-BUDGET-01 · Compute budget assignment

WHEN `Metabolism::calculate` is called  
THE SYSTEM SHALL return a `ComputeBudget` whose every field is at or below the corresponding `HardLimits` field  
AND SHALL select the `QualityTier` based on the organism's current `Behaviour` and `smoothedPressure`.

---

## REQ-BUDGET-02 · Hard limits are absolute

WHEN any internal heuristic, config value, or tuned parameter would produce a `ComputeBudget` that exceeds `HardLimits`  
THE SYSTEM SHALL clamp the budget to `HardLimits` after all other calculations  
AND this clamp SHALL be the final operation before the budget is returned.

---

## REQ-BUDGET-03 · Asymmetric tier pacing

WHEN host pressure increases to a level that requires a lower `QualityTier`  
THE SYSTEM SHALL degrade to the required tier immediately (within one decision interval).

WHEN host pressure decreases and a higher `QualityTier` becomes sustainable  
THE SYSTEM SHALL restore quality by at most one tier per dwell period.

---

## REQ-BODY-01 · Particle pool

WHEN the Body is initialised  
THE SYSTEM SHALL allocate a fixed-capacity particle pool of size `HardLimits.maxParticles`  
AND SHALL activate and deactivate pool entries rather than allocating or freeing memory per frame.

WHEN the current `ComputeBudget.particleLimit` is less than the number of active particles  
THE SYSTEM SHALL deactivate the excess particles within the same simulation tick.

---

## REQ-BODY-02 · Independent simulation clock

WHEN the simulation thread ticks the Body  
THE SYSTEM SHALL use a clock independent of the render clock  
AND a rendered frame SHALL NOT trigger a Brain update or a simulation step.

---

## REQ-DORMANCY-01 · Real dormancy

WHEN the organism enters `Behaviour::Dormant`  
THE SYSTEM SHALL stop normal simulation updates  
AND SHALL stop or event-drive rendering (not merely hide the window)  
AND SHALL save a checkpoint to SQLite before stopping simulation updates  
AND SHALL reduce sensor sampling to a low-frequency heartbeat.

WHEN host conditions recover past the wake threshold and the dwell time has elapsed  
THE SYSTEM SHALL resume normal operation and restore the organism's identity from the last checkpoint.

---

## REQ-MEMORY-01 · Persistent identity

WHEN the application starts  
THE SYSTEM SHALL load the organism's identity, age, and traits from the last valid SQLite checkpoint before starting the simulation.

WHEN no valid checkpoint exists  
THE SYSTEM SHALL initialise a new organism identity with a unique `id` and `age=0`.

WHEN the organism's age after restart differs from the age at the last checkpoint by more than the configured tolerance  
THE SYSTEM SHALL log the discrepancy but SHALL continue with the restored age rather than resetting it.

---

## REQ-MEMORY-02 · Save policy

WHEN the organism enters dormancy, exits dormancy, or transitions to a different `Behaviour`  
THE SYSTEM SHALL write a checkpoint.

WHEN a periodic checkpoint interval elapses  
THE SYSTEM SHALL write a checkpoint.

WHEN the application receives a clean shutdown signal  
THE SYSTEM SHALL write a final checkpoint before exiting.

Checkpoints SHALL use transactions and atomic replacement. DB work SHALL NOT occur on the GUI or render thread.

---

## REQ-UI-01 · Snapshot-only rendering

WHEN the Qt/QML UI renders a frame  
THE SYSTEM SHALL read only from the latest immutable `OrganismSnapshot`  
AND SHALL NOT hold mutable references to any simulation object, Windows handle, or database connection.

---

## REQ-UI-02 · Synthetic telemetry label

WHEN `OrganismSnapshot.telemetry_source == TelemetrySource::Synthetic`  
THE SYSTEM SHALL display a visible indicator in the UI (e.g. "SYNTHETIC" overlay)  
so that demo recordings and screenshots cannot be mistaken for live measurements.

---

## REQ-UI-03 · Budget-coupled animation, companion skinning & locomotion

WHEN `OrganismSnapshot` changes `Behaviour`, `QualityTier`, or pressure delta (`animPulse`)  
THE SYSTEM SHALL derive view-layer animation states (`animState`), pressure-spike flinch, and procedural gait/motion exclusively inside the UI layer (`OrganismViewModel`, `LocomotionEngine`, `AnimationLayer.qml`) without modifying simulation contracts.

WHEN `OrganismSnapshot.dormant == true` or `QualityTier == Tier0_Dormant`  
THE SYSTEM SHALL halt autonomous desktop walking, spring sway, cursor gaze, and non-dormant animation timers.

WHEN the organism is awake and locomotion is enabled  
THE SYSTEM SHALL pace frame-driven locomotion updates to `ComputeBudget::target_render_hz`, pause autonomous walking during pointer hover or open UI panels, and support `[C]` companion skin switching and `[M]` movement toggling persisted via `QSettings`.

---

## REQ-BENCH-01 · Repeatable benchmark

WHEN the benchmark harness runs  
THE SYSTEM SHALL record process CPU (normalised by logical core count), private working set, and committed memory at regular intervals  
AND SHALL produce separate traces for adaptive-complexity and fixed-complexity modes  
AND SHALL record the machine configuration (CPU model, logical cores, RAM, OS version) in the output.

WHEN reporting benchmark results  
THE SYSTEM SHALL state the machine, workload profile, duration and measurement method  
AND SHALL NOT present synthetic-telemetry results as live-host measurements.
