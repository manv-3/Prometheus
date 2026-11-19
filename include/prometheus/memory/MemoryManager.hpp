#pragma once

#include "prometheus/common/OrganismState.hpp"
#include <memory>
#include <string>
#include <cstdint>

namespace prometheus::memory {

enum class SaveTrigger : uint8_t {
    DormancyEntry,
    DormancyExit,
    BehaviourTransition,
    Periodic,
    CleanShutdown
};

/// Current schema version. Bump on breaking schema changes.
static constexpr int32_t kSchemaVersion = 1;

/**
 * @brief Result of a loadCheckpoint operation.
 */
struct LoadResult {
    common::OrganismState state{};
    bool found{false};      // true if a valid checkpoint was loaded
    bool corrupted{false};  // true if the DB was corrupt and we fell back to new
    int32_t schemaVersion{0};
};

/**
 * @brief Persists organism identity, events, and checkpoints to SQLite.
 *
 * All DB work runs on the calling thread (the persistence worker in Phase 3).
 * Never called from the GUI or render thread.
 *
 * Schema is versioned; migration and corrupt-state recovery handled at startup.
 * Checkpoint writes use transactions + atomic replacement.
 *
 * STATUS: Phase 2 — TASK-2.4 real implementation.
 */
class MemoryManager {
public:
    explicit MemoryManager(const std::string& dbPath = "prometheus.db");
    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    /// Load the last valid checkpoint before simulation starts.
    /// Returns found=false if none exists; corrupted=true if DB was corrupt.
    [[nodiscard]] LoadResult loadCheckpoint();

    /// Save a checkpoint. Uses a transaction; atomic replacement.
    /// @param state Current organism state.
    /// @param trigger Why the checkpoint is being saved (for the events log).
    void saveCheckpoint(const common::OrganismState& state, SaveTrigger trigger);

    /// Get the current schema version of the database (0 if not yet created).
    [[nodiscard]] int32_t schemaVersion() const noexcept { return schemaVersion_; }

    /// Close the database. Called by destructor; safe to call manually.
    void close();

private:
    void openDatabase();
    void createSchema();
    void migrateSchema();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    int32_t schemaVersion_{0};
};

} // namespace prometheus::memory
