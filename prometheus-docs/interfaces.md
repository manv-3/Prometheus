# Interfaces and Data Contracts

> **Status: RATIFIED (Provisional Team Contract, `kContractVersion = 2`).**  
> The authoritative C++20 headers live in [`include/prometheus/common/`](../include/prometheus/common/):  
> `HostObservation.hpp`, `OrganismState.hpp`, `ComputeBudget.hpp`, and `OrganismSnapshot.hpp`.  
> The original Phase 0 design sketches below are retained for reference alongside the implemented enum values (`Behaviour`: `Dormant, Resting, Idle, Observing, Exploring, Stressed`; `QualityTier`: `Tier0_Dormant .. Tier4_Optimal`). Any breaking change requires two-dev approval and bumping `kContractVersion`.

## 1. Cross-cutting rules *(proposed)*

| Topic | Rule |
|---|---|
| Time | `std::chrono::steady_clock` for timestamps/intervals. `float` seconds for `deltaTime`. No wall-clock in control logic. |
| Errors | No exceptions across module boundaries in the hot loop. Sensors report via `Metric.valid`. Memory I/O returns an explicit result (value or error code). |
| Missing data | Never zero. `valid=false`, and consumers renormalize or fall back. |
| Ownership | Modules exchange plain value types. Snapshots are immutable and shared via `shared_ptr<const T>`. |
| Threading | Each struct documents who may read/write it. Snapshots cross threads; live objects do not. |
| Determinism | Brain/Metabolism are pure functions of (previous state, observation, deltaTime, config). RNG, if any, is injected and seeded. |
| Versioning | `constexpr int kContractVersion`. Bump on any breaking change. |

## 2. Types

```cpp
// core/Types.h
#include <chrono>
#include <cstdint>

using SteadyTime = std::chrono::steady_clock::time_point;

enum class TelemetrySource : std::uint8_t { Real, Synthetic };

struct Metric {
    float value = 0.0f;
    bool  valid = false;          // default invalid: missing is never zero
};
```

```cpp
// sensory/HostObservation.h
struct HostObservation {
    Metric cpuUsage;              // 0..1
    Metric ramUsage;              // 0..1 (fraction used)
    Metric availableRamMB;
    Metric gpuUsage;              // optional
    Metric gpuMemoryUsage;        // optional
    bool   gpuAvailable = false;
    bool   onBattery    = false;
    SteadyTime sampledAt{};
    TelemetrySource source = TelemetrySource::Real;   // (proposed) label demo data honestly
};
```

```cpp
// brain/Behaviour.h
enum class Behaviour : std::uint8_t { Explore, Idle, Conserve, Recover, Dormant };
```

> **Open naming decision:** the report's state table says Active/Idle/Stressed/Recovery/Dormant, while its policy uses explore/idle/conserve/recover/dormant. Proposed mapping: Active=Explore, Stressed=Conserve, Recovery=Recover. Pick one set in Phase 0 (see [decisions.md](decisions.md)).

```cpp
// brain/OrganismState.h   (proposed: the report does not define it)
struct OrganismState {
    float energy     = 100.0f;    // 0..100
    float health     = 100.0f;    // 0..100
    float stress     = 0.0f;      // 0..100
    float curiosity  = 50.0f;     // 0..100
    float adaptation = 0.0f;      // 0..100
    double ageSeconds = 0.0;      // monotonic
    float smoothedPressure = 0.0f;// 0..1, owned by Brain so Metabolism does not recompute
    Behaviour behaviour = Behaviour::Idle;
    float secondsInBehaviour = 0.0f; // for minimum dwell time
    std::uint32_t generation = 0;
    std::uint64_t id = 0;            // persistent identity
};
```

```cpp
// metabolism/ComputeBudget.h
enum class QualityTier : std::uint8_t { Dormant = 0, Survival, Reduced, Normal, Thriving };

struct ComputeBudget {
    float simulationFrequencyHz = 0.0f;
    int   particleLimit         = 0;
    int   interactionLimit      = 0;
    int   renderFps             = 0;
    int   simulationDetail      = 0;
    bool  advancedEffectsEnabled = false;
    QualityTier tier = QualityTier::Dormant;   // (proposed) explicit tier for logging/UI
};

struct HardLimits {                  // (proposed) non-negotiable caps, loaded from config
    int   maxParticles;
    int   maxInteractions;
    float maxSimulationHz;
    int   maxRenderFps;
    float minSchedulingIntervalMs;
    // bounded queue / history sizes live here too
};
```

```cpp
// core/OrganismSnapshot.h   (proposed fields)
struct OrganismSnapshot {
    std::uint64_t sequence = 0;
    SteadyTime    publishedAt{};
    OrganismState state;
    ComputeBudget budget;
    // measured host values (shown separately from simulated state in the observer panel)
    Metric cpuUsage, ramUsage, availableRamMB;
    TelemetrySource telemetrySource = TelemetrySource::Real;
    // measured self-cost / activity
    int   activeParticles = 0;
    float measuredSimHz   = 0.0f;
    float measuredRenderFps = 0.0f;
    bool  telemetryStale  = false;
    bool  dormant         = false;
};
```

## 3. Module interfaces

```cpp
class SensoryProvider {                         // real or synthetic
public:
    virtual ~SensoryProvider() = default;
    virtual HostObservation latest() const = 0; // thread-safe; never blocks on I/O
};

class Brain {
public:
    OrganismState update(const HostObservation& host,
                         const OrganismState& previous,
                         float deltaTimeSeconds);
};

class Metabolism {
public:
    // Reads organism.smoothedPressure; applies asymmetric degrade/recover and hard limits.
    ComputeBudget calculate(const HostObservation& host,
                            const OrganismState& organism);
};

class Body {
public:
    void update(const ComputeBudget& budget, float deltaTimeSeconds);
};

class MemoryManager {
public:
    void saveCheckpoint(const OrganismState& state);   // errors reported explicitly (proposed result type)
    OrganismState loadCheckpoint();                    // recovers/initializes on corrupt or missing state
};
```

These are starting points. Each implementation must document ownership, units, thread safety and snapshot semantics.

## 4. Change-control protocol

Shared contracts: `HostObservation`, `OrganismState`, `Behaviour`, `ComputeBudget`, `QualityTier`, `HardLimits`, `OrganismSnapshot`, `kContractVersion`.

1. Open a PR labelled `contract-change` describing the need, the diff, and which modules break.
2. **Both other devs approve.** No self-merge.
3. Bump `kContractVersion` if breaking; update stubs and tests in the same PR.
4. Update this document and the status board.
5. Prefer additive changes (new optional fields with safe defaults) over breaking ones.

## 5. Invariants every consumer may rely on

- `OrganismState` numeric fields stay within their documented ranges after `Brain::update`.
- `ComputeBudget` never exceeds `HardLimits`.
- `Dormant` tier ⇒ no normal simulation updates are requested.
- `HostObservation.sampledAt` is monotonic per source.
- A snapshot, once published, is never mutated.
