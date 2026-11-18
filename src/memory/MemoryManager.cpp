#include "prometheus/memory/MemoryManager.hpp"
#include <memory>

namespace prometheus::memory {

// STUB — Phase 0 skeleton. Dev A owns the real implementation (TASK-2.4).
// Real version: SQLite connection, schema_version migration, bounded
// persistence worker queue, atomic checkpoint replacement via transaction.

struct MemoryManager::Impl {
    std::string dbPath;
};

MemoryManager::MemoryManager(const std::string& dbPath)
    : impl_(std::make_unique<Impl>())
{
    impl_->dbPath = dbPath;
}

MemoryManager::~MemoryManager() = default;

common::OrganismState MemoryManager::loadCheckpoint() {
    // STUB: return a default-initialised state (new organism).
    // Real version: open DB, read latest valid checkpoint row, migrate schema.
    return common::OrganismState{};
}

void MemoryManager::saveCheckpoint(const common::OrganismState& /*state*/, SaveTrigger /*trigger*/) {
    // STUB: no-op. Real version: enqueue CheckpointRequest to persistence worker.
}

} // namespace prometheus::memory
