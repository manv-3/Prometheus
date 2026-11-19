#pragma once

#include "prometheus/sensory/SensoryProvider.hpp"
#include "prometheus/platform/windows/CpuMonitor.hpp"
#include "prometheus/platform/windows/MemoryMonitor.hpp"
#include <memory>

namespace prometheus::sensory {

/**
 * @brief Adapts CpuMonitor + MemoryMonitor into the portable SensoryProvider
 *        interface so the simulation thread can consume real host telemetry
 *        without depending on Windows headers.
 *
 * Sampling is done lazily on each latest() call — the sensors are cheap.
 * The caller (SensorWorker) controls sampling frequency.
 *
 * STATUS: Phase 2 integration — wires Dev A's real monitors into the pipeline.
 */
class RealSensorProvider final : public SensoryProvider {
public:
    RealSensorProvider();
    ~RealSensorProvider() override;

    [[nodiscard]] common::HostObservation latest() const override;

private:
    platform::windows::CpuMonitor    cpuMonitor_;
    platform::windows::MemoryMonitor  memoryMonitor_;
};

} // namespace prometheus::sensory
