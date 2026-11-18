#include <gtest/gtest.h>
#include "prometheus/brain/BehaviourFSM.hpp"
#include <limits>

using namespace prometheus;
using namespace prometheus::common;
using namespace prometheus::brain;

namespace {

PressureResult make_fresh_pressure(float raw, float smoothed = 0.0f) {
    PressureResult res;
    res.raw_pressure = raw;
    res.smoothed_pressure = (smoothed > 0.0f) ? smoothed : raw;
    res.is_valid = true;
    res.has_smoothed_value = true;
    res.contributing_signals = 2;
    res.total_valid_weight = 0.65f;
    return res;
}

PressureResult make_stale_pressure(float last_smoothed) {
    PressureResult res;
    res.raw_pressure = 0.0f;
    res.smoothed_pressure = last_smoothed;
    res.is_valid = false;
    res.has_smoothed_value = true;
    res.contributing_signals = 0;
    res.total_valid_weight = 0.0f;
    return res;
}

PressureResult make_unavailable_pressure() {
    PressureResult res;
    res.raw_pressure = 0.0f;
    res.smoothed_pressure = 0.0f;
    res.is_valid = false;
    res.has_smoothed_value = false;
    res.contributing_signals = 0;
    res.total_valid_weight = 0.0f;
    return res;
}

HostObservation make_nominal_observation() {
    HostObservation obs;
    obs.cpu_load = 0.15f;
    obs.cpu_valid = true;
    obs.memory_pressure = 0.20f;
    obs.memory_valid = true;
    obs.is_user_active = false;
    return obs;
}

} // namespace

// ============================================================================
// 1. Static Configuration Validation
// ============================================================================
TEST(BehaviourFSMTest, DefaultConfigurationIsValid) {
    BehaviourFSMConfig cfg;
    EXPECT_TRUE(BehaviourFSM::validate_config(cfg));

    BehaviourFSM fsm(cfg);
    EXPECT_FLOAT_EQ(fsm.config().health_dormant_enter, 0.10f);
}

TEST(BehaviourFSMTest, RejectsInvalidThresholdRanges) {
    BehaviourFSMConfig cfg;

    // Out of range (> 1.0f)
    cfg.health_dormant_enter = 1.05f;
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));

    // Negative (< 0.0f)
    cfg = BehaviourFSMConfig{};
    cfg.stress_enter_threshold = -0.1f;
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));

    // NaN / Inf
    cfg = BehaviourFSMConfig{};
    cfg.fallback_pressure = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));
}

TEST(BehaviourFSMTest, RejectsContradictoryHysteresisBands) {
    BehaviourFSMConfig cfg;

    // Enter must be strictly less than exit for health
    cfg.health_dormant_enter = 0.30f;
    cfg.health_dormant_exit = 0.20f;
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));

    // Stress enter must be strictly greater than stress exit
    cfg = BehaviourFSMConfig{};
    cfg.stress_enter_threshold = 0.50f;
    cfg.stress_exit_threshold = 0.60f;
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));

    // Explore energy enter must be strictly greater than explore exit
    cfg = BehaviourFSMConfig{};
    cfg.energy_explore_enter = 0.40f;
    cfg.energy_explore_exit = 0.50f;
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));
}

TEST(BehaviourFSMTest, RejectsNegativeOrNanDwellTimes) {
    BehaviourFSMConfig cfg;
    cfg.min_dwell_dormant = -1.0f;
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));

    cfg = BehaviourFSMConfig{};
    cfg.min_dwell_resting = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(BehaviourFSM::validate_config(cfg));
}

TEST(BehaviourFSMTest, SetConfigRollbackOnInvalid) {
    BehaviourFSM fsm;
    const auto original_cfg = fsm.config();

    BehaviourFSMConfig invalid_cfg = original_cfg;
    invalid_cfg.energy_rest_enter = 0.90f;
    invalid_cfg.energy_rest_exit = 0.40f; // Inverted!

    EXPECT_FALSE(fsm.set_config(invalid_cfg));
    EXPECT_FLOAT_EQ(fsm.config().energy_rest_enter, original_cfg.energy_rest_enter);
}

// ============================================================================
// 2. Resting vs. Stressed Conflict Resolution
// ============================================================================
TEST(BehaviourFSMTest, LowEnergyWithModerateStressSelectsRestingNotIdle) {
    BehaviourFSM fsm;
    OrganismState state;
    state.energy = 0.15f;
    state.stress = 0.65f;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;

    auto p = make_fresh_pressure(0.30f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // E = 0.15 is below rest_enter (0.20). S = 0.65 is below stress_enter (0.70).
    // Must select Resting, NOT Idle or Stressed!
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
    EXPECT_TRUE(decision.transition_occurred);
    EXPECT_EQ(decision.previous_behaviour, Behaviour::Idle);
}

TEST(BehaviourFSMTest, LowEnergyWithAcuteStressSelectsStressedOverResting) {
    BehaviourFSM fsm;
    OrganismState state;
    state.energy = 0.15f;
    state.stress = 0.75f; // Exceeds stress_enter (0.70)
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;

    auto p = make_fresh_pressure(0.30f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // Priority 2 Stressed preempts Priority 3 Resting
    EXPECT_EQ(decision.behaviour, Behaviour::Stressed);
    EXPECT_TRUE(decision.transition_occurred);
}

TEST(BehaviourFSMTest, StressCooldownDropsImmediatelyIntoRestingIfEnergyStillLow) {
    BehaviourFSM fsm;
    OrganismState state;
    state.energy = 0.15f;
    state.stress = 0.40f; // Drops below stress_exit (0.45)
    state.current_behaviour = Behaviour::Stressed;
    state.dwell_time_seconds = 3.0f; // Exceeds min_dwell_stressed (2.0s)

    auto p = make_fresh_pressure(0.30f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // Stress cleared, organism still depleted -> Immediately transits to Resting
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
    EXPECT_TRUE(decision.transition_occurred);
}

// ============================================================================
// 3. Complete Hysteresis for All Six Behaviours
// ============================================================================
TEST(BehaviourFSMTest, RestingHysteresisBand) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    // 1. Enter Resting at E = 0.19 (<= 0.20)
    state.energy = 0.19f;
    auto d1 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d1.behaviour, Behaviour::Resting);
    EXPECT_TRUE(d1.transition_occurred);

    // 2. Retain Resting at E = 0.35 (above enter, below exit 0.50)
    state.current_behaviour = Behaviour::Resting;
    state.energy = 0.35f;
    auto d2 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d2.behaviour, Behaviour::Resting);
    EXPECT_FALSE(d2.transition_occurred);

    // 3. Exit Resting at E = 0.52 (>= 0.50) after dwell satisfied
    state.energy = 0.52f;
    state.dwell_time_seconds = 4.0f; // min_dwell_resting is 3.0s
    auto d3 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d3.behaviour, Behaviour::Idle);
    EXPECT_TRUE(d3.transition_occurred);
}

TEST(BehaviourFSMTest, StressedHysteresisBand) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    // 1. Enter Stressed at S = 0.72 (>= 0.70)
    state.stress = 0.72f;
    auto d1 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d1.behaviour, Behaviour::Stressed);
    EXPECT_TRUE(d1.transition_occurred);

    // 2. Retain Stressed at S = 0.55 (above exit 0.45)
    state.current_behaviour = Behaviour::Stressed;
    state.stress = 0.55f;
    auto d2 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d2.behaviour, Behaviour::Stressed);
    EXPECT_FALSE(d2.transition_occurred);

    // 3. Exit Stressed at S = 0.42 (<= 0.45) after dwell
    state.stress = 0.42f;
    state.dwell_time_seconds = 3.0f; // min_dwell_stressed is 2.0s
    auto d3 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d3.behaviour, Behaviour::Idle);
    EXPECT_TRUE(d3.transition_occurred);
}

TEST(BehaviourFSMTest, DormantBiologicalHysteresisBand) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.health = 0.08f; // <= 0.10 enter
    state.dwell_time_seconds = 2.0f;
    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    // 1. Enters Dormant on critical health
    auto d1 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d1.behaviour, Behaviour::Dormant);
    EXPECT_TRUE(d1.transition_occurred);
    EXPECT_TRUE(d1.detected_emergencies.biological_collapse);

    // 2. Retains Dormant while health = 0.20 (< exit 0.25)
    state.current_behaviour = Behaviour::Dormant;
    state.health = 0.20f;
    state.dwell_time_seconds = 6.0f; // cooldown satisfied
    emergencies.biological_collapse = true;
    auto d2 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d2.behaviour, Behaviour::Dormant);
    EXPECT_FALSE(d2.transition_occurred);

    // 3. Exits Dormant when health = 0.26 (>= 0.25) and energy >= 0.20
    state.health = 0.26f;
    state.energy = 0.50f;
    auto d3 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d3.behaviour, Behaviour::Idle);
    EXPECT_TRUE(d3.transition_occurred);
    EXPECT_TRUE(d3.cleared_emergencies.biological_collapse);
}

TEST(BehaviourFSMTest, ObservingHysteresisAndUserPresence) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.energy = 0.80f;
    state.stress = 0.10f;
    state.curiosity = 0.50f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.is_user_active = true;
    ActiveEmergencies emergencies;

    // 1. Enters Observing on active user
    auto d1 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d1.behaviour, Behaviour::Observing);
    EXPECT_TRUE(d1.transition_occurred);

    // 2. User stops, but dwell timer not elapsed -> Retains Observing
    state.current_behaviour = Behaviour::Observing;
    state.dwell_time_seconds = 1.0f; // min_dwell_observing is 2.0s
    obs.is_user_active = false;
    auto d2 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d2.behaviour, Behaviour::Observing);
    EXPECT_FALSE(d2.transition_occurred);

    // 3. User inactive and dwell satisfied -> Exits to Exploring or Idle
    state.dwell_time_seconds = 3.0f;
    state.curiosity = 0.40f; // below explore enter
    auto d3 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d3.behaviour, Behaviour::Idle);
    EXPECT_TRUE(d3.transition_occurred);
}

TEST(BehaviourFSMTest, ExploringHysteresisBand) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.energy = 0.70f;
    state.curiosity = 0.65f;
    state.stress = 0.10f;

    auto p = make_fresh_pressure(0.25f);
    auto obs = make_nominal_observation();
    // Present but not discharging
    obs.battery = HostObservation::BatteryInfo{0.80f, false, false};
    ActiveEmergencies emergencies;

    // 1. Enters Exploring
    auto d1 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d1.behaviour, Behaviour::Exploring);
    EXPECT_TRUE(d1.transition_occurred);

    // 2. Retains Exploring at E = 0.55 (above exit 0.45)
    state.current_behaviour = Behaviour::Exploring;
    state.energy = 0.55f;
    auto d2 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d2.behaviour, Behaviour::Exploring);
    EXPECT_FALSE(d2.transition_occurred);

    // 3. Exits Exploring when energy decays to 0.44 (<= 0.45)
    state.energy = 0.44f;
    auto d3 = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d3.behaviour, Behaviour::Idle);
    EXPECT_TRUE(d3.transition_occurred);
}

// ============================================================================
// 4. Protective Transitions and Dwell Bypasses
// ============================================================================
TEST(BehaviourFSMTest, VoluntaryEscalationBlockedByDwellLock) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 0.5f; // min_dwell_idle is 1.0s
    state.energy = 0.80f;
    state.curiosity = 0.70f;
    state.stress = 0.10f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.battery = HostObservation::BatteryInfo{0.90f, false, false};
    ActiveEmergencies emergencies;

    // Exploring is eligible, but dwell lock must prevent voluntary transition
    auto decision = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(decision.behaviour, Behaviour::Idle);
    EXPECT_FALSE(decision.transition_occurred);
}

TEST(BehaviourFSMTest, ProtectiveExploringToRestingOnUnsafePressureBypassesDwell) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Exploring;
    state.dwell_time_seconds = 0.2f; // Locked! min_dwell_exploring is 3.0s
    state.energy = 0.60f;
    state.curiosity = 0.60f;
    state.stress = 0.20f;

    // Fresh pressure rises to 0.48 (exceeds pressure_explore_exit 0.45, but < 0.75 stress enter)
    auto p = make_fresh_pressure(0.48f);
    auto obs = make_nominal_observation();
    obs.battery = HostObservation::BatteryInfo{0.90f, false, false};
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // MUST immediately protective de-escalate to Resting, bypassing dwell lock!
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
    EXPECT_TRUE(decision.transition_occurred);
}

TEST(BehaviourFSMTest, ProtectiveExploringToRestingOnLowEnergyBypassesDwell) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Exploring;
    state.dwell_time_seconds = 0.2f; // Locked!
    state.energy = 0.18f; // Low energy!
    state.curiosity = 0.60f;
    state.stress = 0.20f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.battery = HostObservation::BatteryInfo{0.90f, false, false};
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // MUST immediately de-escalate to Resting
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
    EXPECT_TRUE(decision.transition_occurred);
}

TEST(BehaviourFSMTest, ProtectiveExploringToRestingOnLowBatteryBypassesDwell) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Exploring;
    state.dwell_time_seconds = 0.2f; // Locked!
    state.energy = 0.50f;
    state.curiosity = 0.60f;
    state.stress = 0.20f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    // Low battery discharging (level 0.15 <= 0.20)
    obs.battery = HostObservation::BatteryInfo{0.15f, true, false};
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // MUST immediately de-escalate to Resting
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
    EXPECT_TRUE(decision.transition_occurred);
}

TEST(BehaviourFSMTest, ProtectiveObservingToRestingOnLowEnergyBypassesDwell) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Observing;
    state.dwell_time_seconds = 0.3f; // Locked! min_dwell_observing is 2.0s
    state.energy = 0.15f; // Low energy!

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.is_user_active = true; // User active, but organism exhausted
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // Exhausted organism must rest, bypassing dwell lock and preempting user engagement
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
    EXPECT_TRUE(decision.transition_occurred);
}

// ============================================================================
// 5. Emergency Preemption
// ============================================================================
TEST(BehaviourFSMTest, ThermalThrottlingPreemptsExploringInstantly) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Exploring;
    state.dwell_time_seconds = 0.1f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.thermal = HostObservation::ThermalInfo{0.0f, true}; // Throttling!
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    EXPECT_EQ(decision.behaviour, Behaviour::Dormant);
    EXPECT_TRUE(decision.transition_occurred);
    EXPECT_TRUE(decision.detected_emergencies.thermal_throttling);
}

TEST(BehaviourFSMTest, FreshAcutePressurePreemptsIdleInstantly) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 0.1f;

    auto p = make_fresh_pressure(0.85f); // >= 0.75
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    EXPECT_EQ(decision.behaviour, Behaviour::Stressed);
    EXPECT_TRUE(decision.transition_occurred);
}

// ============================================================================
// 6. Battery Telemetry Policies
// ============================================================================
TEST(BehaviourFSMTest, CriticalBatteryDischargingForcesDormant) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.battery = HostObservation::BatteryInfo{0.04f, true, true}; // Critically low!
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(decision.behaviour, Behaviour::Dormant);
    EXPECT_TRUE(decision.transition_occurred);
    EXPECT_TRUE(decision.detected_emergencies.critical_battery);
}

TEST(BehaviourFSMTest, LowBatteryDischargingForbidsExploringAndWidensResting) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.energy = 0.30f; // <= low_battery_energy_rest_boost (0.35)

    auto p = make_fresh_pressure(0.15f);
    auto obs = make_nominal_observation();
    obs.battery = HostObservation::BatteryInfo{0.18f, true, false}; // Low battery
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
    EXPECT_TRUE(decision.transition_occurred);
}

TEST(BehaviourFSMTest, OrdinaryDischargingTightensExploreThresholds) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.curiosity = 0.70f;
    state.stress = 0.10f;

    // Discharging at 50% battery
    auto obs = make_nominal_observation();
    obs.battery = HostObservation::BatteryInfo{0.50f, true, false};
    ActiveEmergencies emergencies;

    // Candidate 1: E = 0.68, P = 0.25 (meets nominal, fails discharging limits E>=0.75, P<=0.20)
    state.energy = 0.68f;
    auto p1 = make_fresh_pressure(0.25f);
    auto d1 = fsm.evaluate(state, p1, obs, emergencies);
    EXPECT_EQ(d1.behaviour, Behaviour::Idle);

    // Candidate 2: E = 0.78, P = 0.15 (meets tightened limits)
    state.energy = 0.78f;
    auto p2 = make_fresh_pressure(0.15f);
    auto d2 = fsm.evaluate(state, p2, obs, emergencies);
    EXPECT_EQ(d2.behaviour, Behaviour::Exploring);
    EXPECT_TRUE(d2.transition_occurred);
}

TEST(BehaviourFSMTest, UnknownBatteryTelemetryRequiresTightenedConservativeExploring) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.curiosity = 0.70f;
    state.stress = 0.10f;

    auto obs = make_nominal_observation();
    obs.battery = std::nullopt; // Unknown battery!
    ActiveEmergencies emergencies;

    // Scenario A: Meets nominal (E=0.70, P=0.25), but fails Unknown tightened limits (E>=0.75, P<=0.20)
    state.energy = 0.70f;
    auto p1 = make_fresh_pressure(0.25f);
    auto d1 = fsm.evaluate(state, p1, obs, emergencies);
    EXPECT_EQ(d1.behaviour, Behaviour::Idle); // Rejected!

    // Scenario B: Meets conservative limits (E=0.80, P=0.15)
    state.energy = 0.80f;
    auto p2 = make_fresh_pressure(0.15f);
    auto d2 = fsm.evaluate(state, p2, obs, emergencies);
    EXPECT_EQ(d2.behaviour, Behaviour::Exploring); // Accepted!
    EXPECT_TRUE(d2.transition_occurred);
}

TEST(BehaviourFSMTest, PresentNotDischargingAllowsNominalExploreLimits) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.curiosity = 0.70f;
    state.stress = 0.10f;

    auto obs = make_nominal_observation();
    obs.battery = HostObservation::BatteryInfo{0.80f, false, false}; // Present, not discharging
    ActiveEmergencies emergencies;

    // E = 0.68 (>= 0.65), P = 0.28 (<= 0.30)
    state.energy = 0.68f;
    auto p = make_fresh_pressure(0.28f);
    auto d = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(d.behaviour, Behaviour::Exploring);
    EXPECT_TRUE(d.transition_occurred);
}

// ============================================================================
// 7. Pressure Freshness
// ============================================================================
TEST(BehaviourFSMTest, StalePressureCannotTriggerStressed) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;

    // Stale pressure was 0.85f before sensor dropout
    auto p = make_stale_pressure(0.85f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    // Stale pressure cannot trigger Stressed!
    EXPECT_NE(decision.behaviour, Behaviour::Stressed);
    EXPECT_EQ(decision.behaviour, Behaviour::Idle);
}

TEST(BehaviourFSMTest, StalePressureDoesNotTrapOrganismInStressedAfterStressRecovers) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Stressed;
    state.dwell_time_seconds = 3.0f; // dwell satisfied
    state.stress = 0.35f; // Internal stress recovered (<= 0.45)

    // Pressure is stale with last known high value
    auto p = make_stale_pressure(0.85f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    // Must NOT be trapped in Stressed by stale pressure
    EXPECT_EQ(decision.behaviour, Behaviour::Idle);
    EXPECT_TRUE(decision.transition_occurred);
}

TEST(BehaviourFSMTest, UnavailablePressureUsesSafeFallbackAndBlocksExploring) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.energy = 0.90f;
    // Curiosity is above explore_enter (0.60) but below observe_trigger (0.75)
    state.curiosity = 0.65f;
    state.stress = 0.05f;

    auto p = make_unavailable_pressure();
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    // Exploring requires fresh telemetry. Unavailable pressure strictly blocks Exploring.
    EXPECT_EQ(decision.behaviour, Behaviour::Idle);
}

// ============================================================================
// 8. Dormant Recovery and Emergency Persistence
// ============================================================================
TEST(BehaviourFSMTest, MissingThermalTelemetryCannotClearThermalThrottlingEmergency) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Dormant;
    state.dwell_time_seconds = 10.0f; // Cooldown satisfied
    state.health = 1.0f;
    state.energy = 1.0f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.thermal = std::nullopt; // Thermal sensor dropped offline!

    ActiveEmergencies emergencies;
    emergencies.thermal_throttling = true;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // Missing thermal telemetry cannot clear thermal emergency! Must stay Dormant.
    EXPECT_EQ(decision.behaviour, Behaviour::Dormant);
    EXPECT_FALSE(decision.transition_occurred);
    EXPECT_FALSE(decision.cleared_emergencies.thermal_throttling);
}

TEST(BehaviourFSMTest, PositiveThermalHeadroomClearsEmergencyAfterCooldown) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Dormant;
    state.dwell_time_seconds = 6.0f; // Cooldown satisfied (>= 5.0s)
    state.health = 0.80f;
    state.energy = 0.80f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    // Thermal sensor positively reports cold with 50% headroom
    obs.thermal = HostObservation::ThermalInfo{0.50f, false};

    ActiveEmergencies emergencies;
    emergencies.thermal_throttling = true;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    // Emergency cleared! Wakes into Idle (staged wake skips Exploring directly from Dormant)
    EXPECT_EQ(decision.behaviour, Behaviour::Idle);
    EXPECT_TRUE(decision.transition_occurred);
    EXPECT_TRUE(decision.cleared_emergencies.thermal_throttling);
}

TEST(BehaviourFSMTest, ClearThenAssertPreventsConcurrentEmergencyClearance) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Dormant;
    state.dwell_time_seconds = 6.0f;
    state.health = 0.80f;
    state.energy = 0.80f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.thermal = HostObservation::ThermalInfo{0.0f, true}; // Still throttling!

    ActiveEmergencies emergencies;
    emergencies.thermal_throttling = true;

    auto decision = fsm.evaluate(state, p, obs, emergencies);

    EXPECT_TRUE(decision.detected_emergencies.thermal_throttling);
    EXPECT_FALSE(decision.cleared_emergencies.thermal_throttling);

    // Verify Coordinator clear-then-assert formula: active_next = (prev && !cleared) || detected
    bool active_next = (emergencies.thermal_throttling && !decision.cleared_emergencies.thermal_throttling) ||
                       decision.detected_emergencies.thermal_throttling;
    EXPECT_TRUE(active_next);
}

// ============================================================================
// 9. Conflicting Triggers & Priority Cascade
// ============================================================================
TEST(BehaviourFSMTest, PriorityCascadeP1BeatsAll) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.energy = 0.04f;   // Triggers P1 Dormant
    state.stress = 0.90f;   // Triggers P2 Stressed
    // Curiosity is above explore_enter (0.60) but below observe_trigger (0.75)
    state.curiosity = 0.65f; // Triggers P4/P5

    auto p = make_fresh_pressure(0.95f);
    auto obs = make_nominal_observation();
    obs.is_user_active = true; // Triggers P4 Observing
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(decision.behaviour, Behaviour::Dormant);
}

TEST(BehaviourFSMTest, PriorityCascadeP2BeatsP3P4P5P6) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.energy = 0.15f;   // Triggers P3 Resting
    state.stress = 0.85f;   // Triggers P2 Stressed

    auto p = make_fresh_pressure(0.40f);
    auto obs = make_nominal_observation();
    obs.is_user_active = true; // Triggers P4 Observing
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(decision.behaviour, Behaviour::Stressed);
}

TEST(BehaviourFSMTest, PriorityCascadeP3BeatsP4P5P6) {
    BehaviourFSM fsm;
    OrganismState state;
    state.current_behaviour = Behaviour::Idle;
    state.dwell_time_seconds = 2.0f;
    state.energy = 0.15f;   // Triggers P3 Resting
    state.stress = 0.20f;

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    obs.is_user_active = true; // Triggers P4 Observing
    ActiveEmergencies emergencies;

    auto decision = fsm.evaluate(state, p, obs, emergencies);
    EXPECT_EQ(decision.behaviour, Behaviour::Resting);
}

// ============================================================================
// 10. Robustness & Ingress Sanitization
// ============================================================================
TEST(BehaviourFSMTest, HandlesNanAndInfinityInStateGracefully) {
    BehaviourFSM fsm;
    OrganismState corrupt;
    corrupt.energy = std::numeric_limits<float>::quiet_NaN();
    corrupt.health = std::numeric_limits<float>::infinity();
    corrupt.stress = -std::numeric_limits<float>::infinity();
    corrupt.curiosity = std::numeric_limits<float>::quiet_NaN();
    corrupt.adaptation = std::numeric_limits<float>::quiet_NaN();
    corrupt.dwell_time_seconds = -10.0f;
    corrupt.current_behaviour = static_cast<Behaviour>(99); // Invalid enum!

    auto p = make_fresh_pressure(0.20f);
    auto obs = make_nominal_observation();
    ActiveEmergencies emergencies;

    // Must execute cleanly without exception or UB
    auto decision = fsm.evaluate(corrupt, p, obs, emergencies);
    EXPECT_TRUE(decision.behaviour == Behaviour::Idle || decision.behaviour == Behaviour::Observing);
}
