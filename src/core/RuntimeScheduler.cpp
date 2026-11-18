#include "prometheus/core/RuntimeScheduler.hpp"

namespace prometheus::core {

RuntimeScheduler::RuntimeScheduler() {
    reset();
}

void RuntimeScheduler::reset() noexcept {
    const auto now = Clock::now();
    lastDecision_ = now;
    lastSimulation_ = now;
}

float RuntimeScheduler::decisionTick() noexcept {
    const auto now = Clock::now();
    const float dt = std::chrono::duration<float>(now - lastDecision_).count();
    lastDecision_ = now;
    return dt;
}

float RuntimeScheduler::simulationTick() noexcept {
    const auto now = Clock::now();
    const float dt = std::chrono::duration<float>(now - lastSimulation_).count();
    lastSimulation_ = now;
    return dt;
}

} // namespace prometheus::core
