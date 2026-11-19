#include "gtest/gtest.h"
#include "prometheus/memory/MemoryManager.hpp"
#include <cstdio>
#include <cmath>

using prometheus::memory::MemoryManager;
using prometheus::memory::SaveTrigger;
using prometheus::memory::LoadResult;
using prometheus::common::OrganismState;
using prometheus::common::Behaviour;

namespace {
// Helper: get a unique temp DB path for each test
std::string tempDbPath(const char* suffix) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "prometheus_test_%s_%d.db", suffix, rand());
    return buf;
}

// Helper: remove a file if it exists
void removeDb(const std::string& path) {
    std::remove(path.c_str());
    // Also remove WAL and SHM sidecar files
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}
} // namespace

// 1. New database returns found=false (no checkpoint exists yet)
TEST(MemoryManagerTest, NewDatabaseHasNoCheckpoint) {
    auto path = tempDbPath("new");
    removeDb(path);
    {
        MemoryManager mm(path);
        auto result = mm.loadCheckpoint();
        EXPECT_FALSE(result.found);
        EXPECT_FALSE(result.corrupted); // DB was created successfully
    }
    removeDb(path);
}

// 2. Save then load round-trip preserves all fields
TEST(MemoryManagerTest, SaveLoadRoundTripPreservesFields) {
    auto path = tempDbPath("roundtrip");
    removeDb(path);
    {
        MemoryManager mm(path);

        OrganismState saved;
        saved.energy = 0.73f;
        saved.health = 0.88f;
        saved.stress = 0.42f;
        saved.curiosity = 0.61f;
        saved.adaptation = 0.27f;
        saved.current_behaviour = Behaviour::Exploring;
        saved.dwell_time_seconds = 12.5f;

        mm.saveCheckpoint(saved, SaveTrigger::Periodic);

        auto result = mm.loadCheckpoint();
        ASSERT_TRUE(result.found);

        EXPECT_NEAR(result.state.energy, 0.73f, 0.001f);
        EXPECT_NEAR(result.state.health, 0.88f, 0.001f);
        EXPECT_NEAR(result.state.stress, 0.42f, 0.001f);
        EXPECT_NEAR(result.state.curiosity, 0.61f, 0.001f);
        EXPECT_NEAR(result.state.adaptation, 0.27f, 0.001f);
        EXPECT_EQ(result.state.current_behaviour, Behaviour::Exploring);
        EXPECT_NEAR(result.state.dwell_time_seconds, 12.5f, 0.01f);
    }
    removeDb(path);
}

// 3. Save replaces the previous checkpoint (atomic replacement)
TEST(MemoryManagerTest, SaveReplacesPreviousCheckpoint) {
    auto path = tempDbPath("replace");
    removeDb(path);
    {
        MemoryManager mm(path);

        OrganismState first;
        first.energy = 0.90f;
        first.current_behaviour = Behaviour::Exploring;
        mm.saveCheckpoint(first, SaveTrigger::Periodic);

        OrganismState second;
        second.energy = 0.30f;
        second.current_behaviour = Behaviour::Stressed;
        mm.saveCheckpoint(second, SaveTrigger::BehaviourTransition);

        auto result = mm.loadCheckpoint();
        ASSERT_TRUE(result.found);
        // Should have the SECOND state, not the first
        EXPECT_NEAR(result.state.energy, 0.30f, 0.001f);
        EXPECT_EQ(result.state.current_behaviour, Behaviour::Stressed);
    }
    removeDb(path);
}

// 4. Identity survives across restart (new MemoryManager instance, same DB file)
TEST(MemoryManagerTest, IdentitySurvivesAcrossRestart) {
    auto path = tempDbPath("restart");
    removeDb(path);
    {
        MemoryManager mm1(path);
        OrganismState state;
        state.energy = 0.55f;
        state.stress = 0.35f;
        state.current_behaviour = Behaviour::Resting;
        state.dwell_time_seconds = 42.0f;
        mm1.saveCheckpoint(state, SaveTrigger::CleanShutdown);
    }
    // mm1 is destroyed (DB closed). Now open a new MemoryManager pointing at the same file.
    {
        MemoryManager mm2(path);
        auto result = mm2.loadCheckpoint();
        ASSERT_TRUE(result.found);
        EXPECT_NEAR(result.state.energy, 0.55f, 0.001f);
        EXPECT_NEAR(result.state.stress, 0.35f, 0.001f);
        EXPECT_EQ(result.state.current_behaviour, Behaviour::Resting);
        EXPECT_NEAR(result.state.dwell_time_seconds, 42.0f, 0.01f);
    }
    removeDb(path);
}

// 5. NaN and Inf in state are sanitized on save (SQLite cannot store them)
TEST(MemoryManagerTest, NanAndInfSanitizedOnSave) {
    auto path = tempDbPath("nan");
    removeDb(path);
    {
        MemoryManager mm(path);

        OrganismState bad;
        bad.energy = std::numeric_limits<float>::quiet_NaN();
        bad.health = std::numeric_limits<float>::infinity();
        bad.stress = -std::numeric_limits<float>::infinity();
        bad.curiosity = 2.5f; // out of range
        bad.adaptation = -1.0f; // out of range
        bad.current_behaviour = Behaviour::Idle;
        bad.dwell_time_seconds = std::numeric_limits<float>::quiet_NaN();

        // Save the bad state — MemoryManager sanitizes before writing to SQLite
        mm.saveCheckpoint(bad, SaveTrigger::Periodic);

        auto result = mm.loadCheckpoint();
        ASSERT_TRUE(result.found);
        // All loaded values must be finite and in range (sanitized on save)
        EXPECT_TRUE(std::isfinite(result.state.energy));
        EXPECT_TRUE(std::isfinite(result.state.health));
        EXPECT_TRUE(std::isfinite(result.state.stress));
        EXPECT_TRUE(std::isfinite(result.state.curiosity));
        EXPECT_TRUE(std::isfinite(result.state.adaptation));
        EXPECT_TRUE(std::isfinite(result.state.dwell_time_seconds));
        EXPECT_GE(result.state.energy, 0.0f);
        EXPECT_LE(result.state.energy, 1.0f);
        EXPECT_GE(result.state.stress, 0.0f);
        EXPECT_LE(result.state.stress, 1.0f);
        // NaN energy → fallback 0.5, NaN dwell → fallback 0.0
        EXPECT_NEAR(result.state.energy, 0.5f, 0.001f);
        EXPECT_NEAR(result.state.dwell_time_seconds, 0.0f, 0.001f);
    }
    removeDb(path);
}

// 6. Dormant behaviour is preserved across save/load
TEST(MemoryManagerTest, DormantBehaviourPreserved) {
    auto path = tempDbPath("dormant");
    removeDb(path);
    {
        MemoryManager mm(path);
        OrganismState state;
        state.current_behaviour = Behaviour::Dormant;
        state.energy = 0.02f;
        mm.saveCheckpoint(state, SaveTrigger::DormancyEntry);

        auto result = mm.loadCheckpoint();
        ASSERT_TRUE(result.found);
        EXPECT_EQ(result.state.current_behaviour, Behaviour::Dormant);
    }
    removeDb(path);
}

// 7. Schema version is set on creation
TEST(MemoryManagerTest, SchemaVersionSetOnCreation) {
    auto path = tempDbPath("schema");
    removeDb(path);
    {
        MemoryManager mm(path);
        EXPECT_EQ(mm.schemaVersion(), prometheus::memory::kSchemaVersion);
    }
    removeDb(path);
}

// 8. Multiple saves with different triggers all succeed
TEST(MemoryManagerTest, MultipleSavesWithDifferentTriggers) {
    auto path = tempDbPath("triggers");
    removeDb(path);
    {
        MemoryManager mm(path);
        OrganismState state;
        state.energy = 0.5f;
        state.current_behaviour = Behaviour::Idle;

        mm.saveCheckpoint(state, SaveTrigger::DormancyEntry);
        state.energy = 0.4f;
        mm.saveCheckpoint(state, SaveTrigger::DormancyExit);
        state.energy = 0.3f;
        mm.saveCheckpoint(state, SaveTrigger::BehaviourTransition);
        state.energy = 0.2f;
        mm.saveCheckpoint(state, SaveTrigger::Periodic);
        state.energy = 0.1f;
        mm.saveCheckpoint(state, SaveTrigger::CleanShutdown);

        auto result = mm.loadCheckpoint();
        ASSERT_TRUE(result.found);
        EXPECT_NEAR(result.state.energy, 0.1f, 0.001f);
    }
    removeDb(path);
}
