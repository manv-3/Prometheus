#include "prometheus/sensory/RealSensorProvider.hpp"

namespace prometheus::sensory {

RealSensorProvider::RealSensorProvider() = default;
RealSensorProvider::~RealSensorProvider() = default;

common::HostObservation RealSensorProvider::latest() const {
    common::HostObservation obs;
    obs.timestamp = std::chrono::steady_clock::now();

    // Sample CPU (interval-based: first call returns invalid)
    const auto cpu = cpuMonitor_.sample();
    obs.cpu_load  = cpu.load;
    obs.cpu_valid = cpu.valid;

    // Sample memory
    const auto mem = memoryMonitor_.sample();
    obs.memory_pressure = mem.usageFraction;
    obs.memory_valid     = mem.usageValid;

    // Optional telemetry: not available in MVP
    obs.battery  = std::nullopt;
    obs.thermal  = std::nullopt;
    obs.gpu      = std::nullopt;
    obs.is_user_active = false;

    return obs;
}

} // namespace prometheus::sensory
