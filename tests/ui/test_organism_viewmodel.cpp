#include "gtest/gtest.h"

#include "prometheus/ui/OrganismViewModel.hpp"
#include "prometheus/common/OrganismSnapshot.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <fstream>
#include <limits>
#include <memory>

namespace {

using prometheus::common::Behaviour;
using prometheus::common::OrganismSnapshot;
using prometheus::common::QualityTier;
using prometheus::ui::OrganismViewModel;

std::shared_ptr<const OrganismSnapshot> makeSnapshot(
    Behaviour behaviour,
    QualityTier tier,
    float pressure = 0.2f,
    bool dormant = false)
{
    auto snap = std::make_shared<OrganismSnapshot>();
    snap->state.current_behaviour = behaviour;
    snap->budget.tier = tier;
    snap->environmental_pressure = pressure;
    snap->dormant = dormant || (behaviour == Behaviour::Dormant);
    return snap;
}

TEST(OrganismViewModelTest, DefaultsAreIdleAndZeroPulse) {
    OrganismViewModel vm;
    EXPECT_EQ(vm.animState(), QStringLiteral("idle"));
    EXPECT_FLOAT_EQ(vm.animPulse(), 0.0f);
}

TEST(OrganismViewModelTest, MapsAllBehaviourAndTierPairsToAnimState) {
    OrganismViewModel vm;

    const struct {
        Behaviour behaviour;
        QualityTier tier;
        const char* expected;
    } cases[] = {
        {Behaviour::Dormant,   QualityTier::Tier0_Dormant,     "dormant"},
        {Behaviour::Dormant,   QualityTier::Tier4_Optimal,     "dormant"},
        {Behaviour::Resting,   QualityTier::Tier0_Dormant,     "rest_deep"},
        {Behaviour::Resting,   QualityTier::Tier1_Critical,    "rest_deep"},
        {Behaviour::Resting,   QualityTier::Tier2_Constrained, "rest_light"},
        {Behaviour::Resting,   QualityTier::Tier4_Optimal,     "rest_light"},
        {Behaviour::Idle,      QualityTier::Tier1_Critical,    "idle"},
        {Behaviour::Idle,      QualityTier::Tier3_Nominal,     "idle"},
        {Behaviour::Observing, QualityTier::Tier2_Constrained, "alert"},
        {Behaviour::Observing, QualityTier::Tier4_Optimal,     "alert"},
        {Behaviour::Exploring, QualityTier::Tier2_Constrained, "explore_lite"},
        {Behaviour::Exploring, QualityTier::Tier3_Nominal,     "explore_full"},
        {Behaviour::Exploring, QualityTier::Tier4_Optimal,     "explore_full"},
        {Behaviour::Stressed,  QualityTier::Tier1_Critical,    "stressed_critical"},
        {Behaviour::Stressed,  QualityTier::Tier2_Constrained, "stressed_mild"},
        {Behaviour::Stressed,  QualityTier::Tier4_Optimal,     "stressed_mild"},
    };

    for (const auto& c : cases) {
        vm.onSnapshotPublished(makeSnapshot(c.behaviour, c.tier));
        EXPECT_EQ(vm.animState(), QString::fromLatin1(c.expected))
            << "behaviour=" << static_cast<int>(c.behaviour)
            << " tier=" << static_cast<int>(c.tier);
    }
}

TEST(OrganismViewModelTest, FirstSnapshotDoesNotTriggerFalsePulse) {
    OrganismViewModel vm;
    QSignalSpy spy(&vm, &OrganismViewModel::snapshotChanged);

    vm.onSnapshotPublished(makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.75f));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_FLOAT_EQ(vm.animPulse(), 0.0f);
}

TEST(OrganismViewModelTest, UpwardPressureSpikeProducesBoundedPulseAndFallingProducesZero) {
    OrganismViewModel vm;
    vm.onSnapshotPublished(makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.20f));
    EXPECT_FLOAT_EQ(vm.animPulse(), 0.0f);

    // Moderate upward jump: delta = 0.10 -> 0.50
    vm.onSnapshotPublished(makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.30f));
    EXPECT_NEAR(vm.animPulse(), 0.50f, 1e-4f);

    // Huge jump: clamped to 1.0f
    vm.onSnapshotPublished(makeSnapshot(Behaviour::Stressed, QualityTier::Tier2_Constrained, 0.95f));
    EXPECT_FLOAT_EQ(vm.animPulse(), 1.0f);

    // Falling pressure: immediate tickSpike is 0, and once below trailing baseline pulse is 0
    vm.onSnapshotPublished(makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.10f));
    EXPECT_FLOAT_EQ(vm.animPulse(), 0.0f);
}

TEST(OrganismViewModelTest, Smoothed30HzRampTriggersPulseAboveThresholdAndDecaysOnPlateau) {
    OrganismViewModel vm;
    vm.onSnapshotPublished(makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.20f));

    // Simulate EMA-smoothed ramp (0.025 per tick over 10 ticks -> 0.45)
    float peakPulse = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        vm.onSnapshotPublished(
            makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.20f + 0.025f * i));
        peakPulse = std::max(peakPulse, vm.animPulse());
    }
    EXPECT_GT(peakPulse, 0.30f);

    // Once pressure plateaus at 0.45, trailing baseline catches up and pulse decays
    for (int i = 0; i < 60; ++i) {
        vm.onSnapshotPublished(makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.45f));
    }
    EXPECT_LT(vm.animPulse(), 0.05f);
}

TEST(OrganismViewModelTest, DormancySuppressesPulseAndSanitizesNonFinitePressure) {
    OrganismViewModel vm;
    vm.onSnapshotPublished(makeSnapshot(Behaviour::Idle, QualityTier::Tier3_Nominal, 0.10f));

    // Dormant suppresses pulse even on huge pressure jump
    vm.onSnapshotPublished(makeSnapshot(Behaviour::Dormant, QualityTier::Tier0_Dormant, 0.90f, true));
    EXPECT_FLOAT_EQ(vm.animPulse(), 0.0f);

    // Non-finite pressure is sanitized safely to [0, 1]
    vm.onSnapshotPublished(makeSnapshot(
        Behaviour::Idle, QualityTier::Tier3_Nominal, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_GE(vm.animPulse(), 0.0f);
    EXPECT_LE(vm.animPulse(), 1.0f);

    vm.onSnapshotPublished(makeSnapshot(
        Behaviour::Idle, QualityTier::Tier3_Nominal, std::numeric_limits<float>::infinity()));
    EXPECT_GE(vm.animPulse(), 0.0f);
    EXPECT_LE(vm.animPulse(), 1.0f);
}

TEST(OrganismViewModelTest, DefaultsToEmberCatAndListsAllBuiltInCharacters) {
    OrganismViewModel vm(false, QString());

    EXPECT_EQ(vm.characterId(), QStringLiteral("cat"));
    EXPECT_EQ(vm.characterName(), QStringLiteral("Ember Cat"));
    EXPECT_EQ(vm.characterSource(), QStringLiteral("qrc:/Prometheus/resources/organism.svg"));
    EXPECT_TRUE(vm.customCharacterPath().isEmpty());
    EXPECT_TRUE(vm.customCharacterSource().isEmpty());

    const QVariantList chars = vm.availableCharacters();
    ASSERT_EQ(chars.size(), 5);
    EXPECT_EQ(chars[0].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cat"));
    EXPECT_EQ(chars[1].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("owl"));
    EXPECT_EQ(chars[2].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("fox"));
    EXPECT_EQ(chars[3].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("bot"));
    EXPECT_EQ(chars[4].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("slime"));
}

TEST(OrganismViewModelTest, SelectsBuiltInCharactersAndRejectsUnknownIds) {
    OrganismViewModel vm(false, QString());
    QSignalSpy spy(&vm, &OrganismViewModel::characterChanged);

    EXPECT_TRUE(vm.selectCharacter(QStringLiteral("owl")));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(vm.characterId(), QStringLiteral("owl"));
    EXPECT_EQ(vm.characterName(), QStringLiteral("Aegis Owl"));
    EXPECT_EQ(vm.characterSource(), QStringLiteral("qrc:/Prometheus/resources/characters/owl.svg"));

    // Selecting the already-active character does not emit redundant signals
    EXPECT_TRUE(vm.selectCharacter(QStringLiteral("owl")));
    EXPECT_EQ(spy.count(), 1);

    // Unknown IDs fail cleanly and keep current selection intact
    EXPECT_FALSE(vm.selectCharacter(QStringLiteral("dragon")));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(vm.characterId(), QStringLiteral("owl"));
}

TEST(OrganismViewModelTest, CustomCharacterValidatesLocalImageFilesOnly) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString svgPath = tempDir.filePath(QStringLiteral("my sprite.svg"));
    {
        std::ofstream out(svgPath.toStdString());
        out << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'/>";
    }
    const QString txtPath = tempDir.filePath(QStringLiteral("notes.txt"));
    {
        std::ofstream out(txtPath.toStdString());
        out << "not an image";
    }

    OrganismViewModel vm(false, QString());
    QSignalSpy spy(&vm, &OrganismViewModel::characterChanged);

    // Reject remote URLs, non-existent paths, and non-image extensions
    EXPECT_FALSE(vm.selectCustomCharacter(QStringLiteral("https://example.com/avatar.svg")));
    EXPECT_FALSE(vm.selectCustomCharacter(tempDir.filePath(QStringLiteral("missing.svg"))));
    EXPECT_FALSE(vm.selectCustomCharacter(txtPath));
    EXPECT_EQ(spy.count(), 0);
    EXPECT_EQ(vm.characterId(), QStringLiteral("cat"));

    // Accept local file:// URL with spaces
    const QString fileUrl = QUrl::fromLocalFile(svgPath).toString();
    EXPECT_TRUE(vm.selectCustomCharacter(fileUrl));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(vm.characterId(), QStringLiteral("custom"));
    EXPECT_EQ(vm.characterName(), QStringLiteral("my sprite"));
    EXPECT_FALSE(vm.customCharacterPath().isEmpty());
    EXPECT_EQ(vm.characterSource(), vm.customCharacterSource());

    // Switching back to a preset remembers the custom slot so "custom" can be re-selected
    EXPECT_TRUE(vm.selectCharacter(QStringLiteral("fox")));
    EXPECT_EQ(vm.characterId(), QStringLiteral("fox"));
    EXPECT_TRUE(vm.selectCharacter(QStringLiteral("custom")));
    EXPECT_EQ(vm.characterId(), QStringLiteral("custom"));
}

TEST(OrganismViewModelTest, CharacterSelectionPersistsAcrossRestartAndFallsBackIfMissing) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString iniPath = tempDir.filePath(QStringLiteral("ui.ini"));

    {
        OrganismViewModel vm1(true, iniPath);
        EXPECT_TRUE(vm1.selectCharacter(QStringLiteral("bot")));
    }
    {
        OrganismViewModel vm2(true, iniPath);
        EXPECT_EQ(vm2.characterId(), QStringLiteral("bot"));
        EXPECT_EQ(vm2.characterName(), QStringLiteral("Atlas Bot"));
    }

    // Missing custom file on restart falls back cleanly to Ember Cat
    const QString transientSvg = tempDir.filePath(QStringLiteral("temp.svg"));
    {
        std::ofstream out(transientSvg.toStdString());
        out << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'/>";
    }
    {
        OrganismViewModel vm3(true, iniPath);
        EXPECT_TRUE(vm3.selectCustomCharacter(transientSvg));
    }
    ASSERT_TRUE(QFile::remove(transientSvg));
    {
        OrganismViewModel vm4(true, iniPath);
        EXPECT_EQ(vm4.characterId(), QStringLiteral("cat"));
    }
}

} // namespace
