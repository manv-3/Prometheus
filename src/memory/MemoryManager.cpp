#include "prometheus/memory/MemoryManager.hpp"
#include <sqlite3.h>
#include <memory>
#include <cstring>
#include <cmath>
#include <chrono>

namespace prometheus::memory {

// ---------------------------------------------------------------------------
// Real SQLite MemoryManager implementation (TASK-2.4).
//
// Schema (versioned, kSchemaVersion = 1):
//   schema_version: tracks the schema version
//   organism:       single-row table for identity + drives
//   events:         append-only log of significant events
//   checkpoints:    latest organism state, atomically replaced per save
//
// All operations use transactions. Corrupt or missing DB → new organism.
// ---------------------------------------------------------------------------

namespace {

const char* kCreateSchemaV1 = R"SQL(
    CREATE TABLE IF NOT EXISTS schema_version (
        version INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS organism (
        id              INTEGER PRIMARY KEY CHECK (id = 1),
        energy          REAL NOT NULL,
        health          REAL NOT NULL,
        stress          REAL NOT NULL,
        curiosity       REAL NOT NULL,
        adaptation      REAL NOT NULL,
        behaviour       INTEGER NOT NULL,
        dwell_time      REAL NOT NULL
    );

    CREATE TABLE IF NOT EXISTS events (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp   INTEGER NOT NULL,
        event_type   TEXT NOT NULL,
        description TEXT
    );

    CREATE TABLE IF NOT EXISTS checkpoints (
        id              INTEGER PRIMARY KEY CHECK (id = 1),
        timestamp       INTEGER NOT NULL,
        energy          REAL NOT NULL,
        health          REAL NOT NULL,
        stress          REAL NOT NULL,
        curiosity       REAL NOT NULL,
        adaptation      REAL NOT NULL,
        behaviour       INTEGER NOT NULL,
        dwell_time      REAL NOT NULL
    );
)SQL";

const char* kTriggerToString[] = {
    "DormancyEntry",
    "DormancyExit",
    "BehaviourTransition",
    "Periodic",
    "CleanShutdown"
};

} // namespace

struct MemoryManager::Impl {
    sqlite3* db{nullptr};
    std::string dbPath;
};

MemoryManager::MemoryManager(const std::string& dbPath)
    : impl_(std::make_unique<Impl>())
{
    impl_->dbPath = dbPath;
    openDatabase();
}

MemoryManager::~MemoryManager() { close(); }

void MemoryManager::openDatabase() {
    int rc = sqlite3_open(impl_->dbPath.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        // Corrupt or inaccessible — mark as corrupted, use in-memory fallback
        if (impl_->db) {
            sqlite3_close(impl_->db);
            impl_->db = nullptr;
        }
        schemaVersion_ = 0;
        return;
    }

    // Enable WAL mode for better concurrency (persistence worker + reads)
    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    createSchema();
    migrateSchema();
}

void MemoryManager::createSchema() {
    if (!impl_->db) return;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(impl_->db, kCreateSchemaV1, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        // Schema creation failed — DB is corrupt
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
        schemaVersion_ = 0;
        return;
    }

    // Write schema version if not already present
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(impl_->db,
        "INSERT OR IGNORE INTO schema_version (version) VALUES (?);",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, kSchemaVersion);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Read back the schema version
    rc = sqlite3_prepare_v2(impl_->db,
        "SELECT version FROM schema_version LIMIT 1;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        schemaVersion_ = sqlite3_column_int(stmt, 0);
    }
    if (stmt) sqlite3_finalize(stmt);
}

void MemoryManager::migrateSchema() {
    // Future: handle migrations from schemaVersion_ < kSchemaVersion
    // For now, v1 is the only version — nothing to migrate.
}

void MemoryManager::close() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

// ---- Load checkpoint ----

LoadResult MemoryManager::loadCheckpoint() {
    LoadResult result{};
    result.schemaVersion = schemaVersion_;

    if (!impl_->db) {
        result.corrupted = true;
        return result;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT energy, health, stress, curiosity, adaptation, behaviour, dwell_time "
        "FROM checkpoints WHERE id = 1;",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        result.corrupted = true;
        if (stmt) sqlite3_finalize(stmt);
        return result;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result.state.energy     = static_cast<float>(sqlite3_column_double(stmt, 0));
        result.state.health     = static_cast<float>(sqlite3_column_double(stmt, 1));
        result.state.stress     = static_cast<float>(sqlite3_column_double(stmt, 2));
        result.state.curiosity  = static_cast<float>(sqlite3_column_double(stmt, 3));
        result.state.adaptation = static_cast<float>(sqlite3_column_double(stmt, 4));
        result.state.current_behaviour = static_cast<common::Behaviour>(sqlite3_column_int(stmt, 5));
        result.state.dwell_time_seconds = static_cast<float>(sqlite3_column_double(stmt, 6));
        result.found = true;
    }

    sqlite3_finalize(stmt);

    // Sanitize loaded values (conservative fallbacks for NaN/Inf)
    auto safeClamp = [](float v, float lo, float hi, float fallback) -> float {
        if (!std::isfinite(v)) return fallback;
        return std::max(lo, std::min(hi, v));
    };
    result.state.energy     = safeClamp(result.state.energy, 0.0f, 1.0f, 0.5f);
    result.state.health     = safeClamp(result.state.health, 0.0f, 1.0f, 0.5f);
    result.state.stress     = safeClamp(result.state.stress, 0.0f, 1.0f, 0.5f);
    result.state.curiosity  = safeClamp(result.state.curiosity, 0.0f, 1.0f, 0.5f);
    result.state.adaptation = safeClamp(result.state.adaptation, 0.0f, 1.0f, 0.0f);
    result.state.dwell_time_seconds = safeClamp(result.state.dwell_time_seconds, 0.0f, 315360000.0f, 0.0f);

    return result;
}

// ---- Save checkpoint ----

void MemoryManager::saveCheckpoint(const common::OrganismState& state, SaveTrigger trigger) {
    if (!impl_->db) return;

    // Sanitize values before saving — SQLite cannot store NaN/Inf
    auto safeClamp = [](float v, float lo, float hi, float fallback) -> double {
        if (!std::isfinite(v)) return static_cast<double>(fallback);
        return static_cast<double>(std::max(lo, std::min(hi, v)));
    };
    double energy = safeClamp(state.energy, 0.0f, 1.0f, 0.5f);
    double health = safeClamp(state.health, 0.0f, 1.0f, 0.5f);
    double stress = safeClamp(state.stress, 0.0f, 1.0f, 0.5f);
    double curiosity = safeClamp(state.curiosity, 0.0f, 1.0f, 0.5f);
    double adaptation = safeClamp(state.adaptation, 0.0f, 1.0f, 0.0f);
    double dwellTime = safeClamp(state.dwell_time_seconds, 0.0f, 315360000.0f, 0.0f);

    // Use a transaction for atomic replacement
    sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // 1. Upsert the checkpoint (INSERT OR REPLACE, id = 1 for single-row)
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "INSERT OR REPLACE INTO checkpoints "
        "(id, timestamp, energy, health, stress, curiosity, adaptation, behaviour, dwell_time) "
        "VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        if (stmt) sqlite3_finalize(stmt);
        return;
    }

    // Timestamp as Unix epoch seconds (for event log only — not for control logic)
    auto now = std::chrono::steady_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    sqlite3_bind_int64(stmt, 1, epoch);
    sqlite3_bind_double(stmt, 2, energy);
    sqlite3_bind_double(stmt, 3, health);
    sqlite3_bind_double(stmt, 4, stress);
    sqlite3_bind_double(stmt, 5, curiosity);
    sqlite3_bind_double(stmt, 6, adaptation);
    sqlite3_bind_int(stmt, 7, static_cast<int>(state.current_behaviour));
    sqlite3_bind_double(stmt, 8, dwellTime);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }

    // 2. Log the event
    rc = sqlite3_prepare_v2(impl_->db,
        "INSERT INTO events (timestamp, event_type, description) VALUES (?, ?, ?);",
        -1, &stmt, nullptr);

    if (rc == SQLITE_OK) {
        int triggerIdx = static_cast<int>(trigger);
        const char* triggerName = (triggerIdx >= 0 && triggerIdx < 5)
            ? kTriggerToString[triggerIdx] : "Unknown";

        sqlite3_bind_int64(stmt, 1, epoch);
        sqlite3_bind_text(stmt, 2, triggerName, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, nullptr, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, nullptr);
}

} // namespace prometheus::memory
