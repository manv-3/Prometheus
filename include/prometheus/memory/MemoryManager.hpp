#pragma once

#include "prometheus/common/OrganismState.hpp"
#include <string>

namespace prometheus::memory {

enum class SaveTrigger : uint8_t {
    DormancyEntry,
    DormancyExit,
    BehaviourTransition,
    Periodic,
    CleanShutdown
};

/**
 * @brief Persists organism identity, events, and checkpoints to SQLite.
 *
 * All DB work runs on the persistence worker thread — never on the GUI or
 * render thread. Enqueues checkpoint requests via a bounded queue.
 *
 * Schema versioned; migration and corrupt-state recovery handled at startup.
 *
 * STATUS: STUB — owned by Dev A (TASK-2.4).
 */
class MemoryManager {
public:
    explicit MemoryManager(const std::string& dbPath = "prometheus.db");
    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    /// Load the last valid checkpoint before simulation starts.
    /// Returns a freshly initialised state if none exists or on corruption.
    [[nodiscard]] common::OrganismState loadCheckpoint();

    /// Enqueue a checkpoint write (non-blocking; handled by persistence worker).
    void saveCheckpoint(const common::OrganismState& state, SaveTrigger trigger);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::memory
