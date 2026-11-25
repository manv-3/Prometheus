#include "gtest/gtest.h"

#include "prometheus/ui/LocomotionEngine.hpp"

#include <cmath>
#include <limits>

namespace {

using prometheus::common::Behaviour;
using prometheus::common::QualityTier;
using prometheus::ui::LocomotionConfig;
using prometheus::ui::LocomotionEngine;
using prometheus::ui::LocomotionInput;
using prometheus::ui::LocomotionMode;
using prometheus::ui::LocomotionOutput;

LocomotionInput makeBaseInput() {
    LocomotionInput in{};
    in.dtSeconds = 1.0f / 60.0f;
    in.behaviour = Behaviour::Idle;
    in.tier = QualityTier::Tier3_Nominal;
    in.dormant = false;
    in.energy = 0.9f;
    in.curiosity = 0.7f;
    in.stress = 0.1f;
    in.locomotionEnabled = true;
    in.dragging = false;
    in.interactionBlocked = false;
    in.windowX = 600.0f;
    in.windowY = 400.0f;
    in.stageWidth = 280.0f;
    in.stageHeight = 300.0f;
    in.charBottomOffsetY = 185.0f;
    in.workLeft = 0.0f;
    in.workTop = 0.0f;
    in.workWidth = 1920.0f;
    in.workHeight = 1040.0f;
    in.cursorValid = false;
    return in;
}

TEST(LocomotionEngineTest, DeterminismWithIdenticalSeedAndInputs) {
    LocomotionEngine a(12345u);
    LocomotionEngine b(12345u);

    LocomotionInput inA = makeBaseInput();
    inA.behaviour = Behaviour::Exploring;
    inA.tier = QualityTier::Tier4_Optimal;
    LocomotionInput inB = inA;

    for (int i = 0; i < 600; ++i) {
        const LocomotionOutput outA = a.step(inA);
        const LocomotionOutput outB = b.step(inB);
        EXPECT_EQ(outA.mode, outB.mode);
        EXPECT_EQ(outA.isWalking, outB.isWalking);
        EXPECT_EQ(outA.facingDir, outB.facingDir);
        EXPECT_FLOAT_EQ(outA.targetWindowX, outB.targetWindowX);
        EXPECT_FLOAT_EQ(outA.walkPhase, outB.walkPhase);
        EXPECT_FLOAT_EQ(outA.walkIntensity, outB.walkIntensity);
        if (outA.shouldMoveWindow) {
            inA.windowX = outA.targetWindowX;
            inB.windowX = outB.targetWindowX;
        }
    }
}

TEST(LocomotionEngineTest, DormancyImmediatelyHaltsWalkingSwayAndGaze) {
    LocomotionEngine engine(42u);
    LocomotionInput in = makeBaseInput();
    in.behaviour = Behaviour::Exploring;
    in.tier = QualityTier::Tier4_Optimal;

    // Advance until walking starts
    bool startedWalk = false;
    for (int i = 0; i < 400; ++i) {
        const LocomotionOutput out = engine.step(in);
        if (out.shouldMoveWindow) in.windowX = out.targetWindowX;
        if (out.isWalking && out.walkIntensity > 0.2f) {
            startedWalk = true;
            break;
        }
    }
    ASSERT_TRUE(startedWalk);

    // Trigger Dormant
    in.behaviour = Behaviour::Dormant;
    in.tier = QualityTier::Tier0_Dormant;
    in.dormant = true;
    const LocomotionOutput dormantOut = engine.step(in);

    EXPECT_EQ(dormantOut.mode, LocomotionMode::Stationary);
    EXPECT_FALSE(dormantOut.isWalking);
    EXPECT_FALSE(dormantOut.shouldMoveWindow);
    EXPECT_FALSE(dormantOut.needsContinuousTick);
    EXPECT_FLOAT_EQ(dormantOut.walkIntensity, 0.0f);
    EXPECT_FLOAT_EQ(dormantOut.walkPhase, 0.0f);
    EXPECT_FLOAT_EQ(dormantOut.swayRotationDeg, 0.0f);
    EXPECT_FLOAT_EQ(dormantOut.gazeLeanDeg, 0.0f);
}

TEST(LocomotionEngineTest, StressedAndRestingNeverInitiateWalks) {
    for (Behaviour b : {Behaviour::Stressed, Behaviour::Resting}) {
        LocomotionEngine engine(99u);
        LocomotionInput in = makeBaseInput();
        in.behaviour = b;
        in.tier = QualityTier::Tier3_Nominal;
        const float startX = in.windowX;

        for (int i = 0; i < 1200; ++i) {
            const LocomotionOutput out = engine.step(in);
            EXPECT_FALSE(out.isWalking);
            EXPECT_FALSE(out.shouldMoveWindow);
            EXPECT_FLOAT_EQ(in.windowX, startX);
        }
    }
}

TEST(LocomotionEngineTest, QualityTierGatesIdleAndScalesExploreSpeed) {
    // Tier 1 Critical suppresses Idle walking
    {
        LocomotionEngine engine(7u);
        LocomotionInput in = makeBaseInput();
        in.behaviour = Behaviour::Idle;
        in.tier = QualityTier::Tier1_Critical;
        for (int i = 0; i < 900; ++i) {
            const LocomotionOutput out = engine.step(in);
            EXPECT_FALSE(out.isWalking);
        }
    }

    // Explore at Tier 4 is faster than Explore at Tier 2 (explore_lite)
    auto measureExploreSpeed = [](QualityTier tier) {
        LocomotionEngine engine(7u);
        LocomotionInput in = makeBaseInput();
        in.behaviour = Behaviour::Exploring;
        in.tier = tier;
        for (int i = 0; i < 600; ++i) {
            const LocomotionOutput out = engine.step(in);
            if (out.isWalking) return out.walkSpeedPxPerSec;
        }
        return 0.0f;
    };

    const float fullSpeed = measureExploreSpeed(QualityTier::Tier4_Optimal);
    const float liteSpeed = measureExploreSpeed(QualityTier::Tier2_Constrained);
    EXPECT_GT(fullSpeed, 80.0f);
    EXPECT_GT(liteSpeed, 30.0f);
    EXPECT_GT(fullSpeed, liteSpeed);
}

TEST(LocomotionEngineTest, EdgeAwarenessTurnsAwayFromMonitorBordersAndClamps) {
    // Near right border -> must walk left (-1)
    {
        LocomotionEngine engine(11u);
        LocomotionInput in = makeBaseInput();
        in.behaviour = Behaviour::Exploring;
        in.tier = QualityTier::Tier4_Optimal;
        const float maxWinX = in.workLeft + in.workWidth - in.stageWidth;
        in.windowX = maxWinX - 4.0f;

        bool walkedFirstCycle = false;
        for (int i = 0; i < 400; ++i) {
            const LocomotionOutput out = engine.step(in);
            if (out.shouldMoveWindow) in.windowX = out.targetWindowX;
            if (out.isWalking && !walkedFirstCycle) {
                EXPECT_EQ(out.facingDir, -1);
                walkedFirstCycle = true;
            }
            EXPECT_GE(in.windowX, in.workLeft);
            EXPECT_LE(in.windowX, maxWinX);
        }
        EXPECT_TRUE(walkedFirstCycle);
    }

    // Near left border -> must walk right (+1)
    {
        LocomotionEngine engine(11u);
        LocomotionInput in = makeBaseInput();
        in.behaviour = Behaviour::Exploring;
        in.tier = QualityTier::Tier4_Optimal;
        in.windowX = in.workLeft + 4.0f;

        bool walkedFirstCycle = false;
        for (int i = 0; i < 400; ++i) {
            const LocomotionOutput out = engine.step(in);
            if (out.shouldMoveWindow) in.windowX = out.targetWindowX;
            if (out.isWalking && !walkedFirstCycle) {
                EXPECT_EQ(out.facingDir, 1);
                walkedFirstCycle = true;
            }
            EXPECT_GE(in.windowX, in.workLeft);
            EXPECT_LE(in.windowX, in.workLeft + in.workWidth - in.stageWidth);
        }
        EXPECT_TRUE(walkedFirstCycle);
    }
}

TEST(LocomotionEngineTest, SubPixelAccumulationMovesWindowAtLowSpeedAndHighFps) {
    LocomotionEngine engine(3u);
    LocomotionInput in = makeBaseInput();
    in.behaviour = Behaviour::Idle;
    in.tier = QualityTier::Tier3_Nominal;
    in.dtSeconds = 1.0f / 120.0f; // ~0.4 px per frame during idle walk (~50 px/s)
    const float startX = in.windowX;

    bool sawWalk = false;
    for (int i = 0; i < 2000; ++i) {
        const LocomotionOutput out = engine.step(in);
        if (out.shouldMoveWindow) in.windowX = out.targetWindowX;
        if (out.isWalking) sawWalk = true;
    }
    EXPECT_TRUE(sawWalk);
    EXPECT_GT(std::abs(in.windowX - startX), 15.0f);
}

TEST(LocomotionEngineTest, InteractionBlockedPausesAutonomousWalk) {
    LocomotionEngine engine(55u);
    LocomotionInput in = makeBaseInput();
    in.behaviour = Behaviour::Exploring;
    in.tier = QualityTier::Tier4_Optimal;

    bool started = false;
    for (int i = 0; i < 400; ++i) {
        const LocomotionOutput out = engine.step(in);
        if (out.shouldMoveWindow) in.windowX = out.targetWindowX;
        if (out.isWalking) {
            started = true;
            break;
        }
    }
    ASSERT_TRUE(started);

    in.interactionBlocked = true;
    const LocomotionOutput blocked = engine.step(in);
    EXPECT_FALSE(blocked.isWalking);
}

TEST(LocomotionEngineTest, DragSpringSwayReactsToVelocityAndSettlesOnRelease) {
    LocomotionEngine engine(1u);
    LocomotionInput in = makeBaseInput();
    in.locomotionEnabled = false;
    in.dragging = true;
    EXPECT_EQ(engine.step(in).mode, LocomotionMode::Dragging); // press frame

    // Drag rapidly rightward -> inertia leans body negative (left)
    float minSway = 0.0f;
    for (int i = 0; i < 15; ++i) {
        in.windowX += 12.0f;
        const LocomotionOutput out = engine.step(in);
        EXPECT_EQ(out.mode, LocomotionMode::Dragging);
        minSway = std::min(minSway, out.swayRotationDeg);
    }
    EXPECT_LT(minSway, -3.0f);

    // Release -> spring settles smoothly toward 0
    in.dragging = false;
    LocomotionOutput afterRelease{};
    for (int i = 0; i < 180; ++i) {
        afterRelease = engine.step(in);
    }
    EXPECT_NEAR(afterRelease.swayRotationDeg, 0.0f, 0.25f);
    EXPECT_FALSE(afterRelease.needsContinuousTick);
}

TEST(LocomotionEngineTest, EdgePeekSnapAndUnsnapHysteresis) {
    LocomotionEngine engine(1u);
    LocomotionInput in = makeBaseInput();
    in.dragging = true;
    in.windowX = 100.0f;
    EXPECT_EQ(engine.step(in).edgePeekSide, 0);

    // Drag to left monitor edge (<= 14 px) -> snaps to left peek
    in.windowX = 6.0f;
    LocomotionOutput out = engine.step(in);
    EXPECT_EQ(out.edgePeekSide, -1);

    // Release -> glides into tucked peek position facing inward (+1)
    in.dragging = false;
    for (int i = 0; i < 60; ++i) {
        out = engine.step(in);
        if (out.shouldMoveWindow) in.windowX = out.targetWindowX;
    }
    EXPECT_EQ(out.mode, LocomotionMode::EdgePeek);
    EXPECT_EQ(out.facingDir, 1);
    EXPECT_LT(in.windowX, in.workLeft);

    // Drag slightly (within unsnap threshold) -> stays snapped
    in.dragging = true;
    in.windowX = 20.0f;
    out = engine.step(in);
    EXPECT_EQ(out.edgePeekSide, -1);

    // Drag past unsnap threshold (> 34 px) -> unsnaps
    in.windowX = 60.0f;
    out = engine.step(in);
    EXPECT_EQ(out.edgePeekSide, 0);
}

TEST(LocomotionEngineTest, TaskbarSittingTriggersInIdleAndStandsInExploring) {
    LocomotionEngine engine(21u);
    LocomotionInput in = makeBaseInput();
    // Place character bottom right on workBottom (1040)
    in.windowY = 1040.0f - in.charBottomOffsetY;
    in.behaviour = Behaviour::Idle;

    LocomotionOutput out = engine.step(in);
    EXPECT_TRUE(out.isSitting);
    EXPECT_FALSE(out.isWalking);

    // Transitioning to Exploring stands up to patrol along the taskbar
    in.behaviour = Behaviour::Exploring;
    in.tier = QualityTier::Tier4_Optimal;
    out = engine.step(in);
    EXPECT_FALSE(out.isSitting);
}

TEST(LocomotionEngineTest, CursorGazeTracksInObservingAndClearsWhenDormant) {
    LocomotionEngine engine(31u);
    LocomotionInput in = makeBaseInput();
    in.behaviour = Behaviour::Observing;
    in.tier = QualityTier::Tier3_Nominal;
    in.windowX = 500.0f;
    in.cursorValid = true;
    in.cursorX = 950.0f; // well to the right of stage center (640)
    in.cursorY = 450.0f;

    LocomotionOutput out{};
    for (int i = 0; i < 45; ++i) {
        out = engine.step(in);
    }
    EXPECT_EQ(out.mode, LocomotionMode::ObserveGaze);
    EXPECT_EQ(out.facingDir, 1);
    EXPECT_GT(out.gazeLeanDeg, 1.5f);
    EXPECT_GT(out.gazeShiftX, 1.0f);

    // Move cursor to the left
    in.cursorX = 200.0f;
    for (int i = 0; i < 60; ++i) {
        out = engine.step(in);
    }
    EXPECT_EQ(out.facingDir, -1);
    EXPECT_LT(out.gazeLeanDeg, -1.5f);
    EXPECT_LT(out.gazeShiftX, -1.0f);

    // Dormant resets gaze immediately
    in.dormant = true;
    in.tier = QualityTier::Tier0_Dormant;
    out = engine.step(in);
    EXPECT_FLOAT_EQ(out.gazeLeanDeg, 0.0f);
    EXPECT_FLOAT_EQ(out.gazeShiftX, 0.0f);
}

TEST(LocomotionEngineTest, SanitizesNonFiniteInputsSafely) {
    LocomotionEngine engine(44u);
    LocomotionInput in = makeBaseInput();
    in.dtSeconds = std::numeric_limits<float>::quiet_NaN();
    in.windowX = std::numeric_limits<float>::infinity();
    in.cursorX = -std::numeric_limits<float>::infinity();
    in.cursorValid = true;

    const LocomotionOutput out = engine.step(in);
    EXPECT_TRUE(std::isfinite(out.targetWindowX));
    EXPECT_TRUE(std::isfinite(out.walkPhase));
    EXPECT_TRUE(std::isfinite(out.walkIntensity));
    EXPECT_TRUE(std::isfinite(out.swayRotationDeg));
    EXPECT_TRUE(std::isfinite(out.gazeLeanDeg));
}

} // namespace
