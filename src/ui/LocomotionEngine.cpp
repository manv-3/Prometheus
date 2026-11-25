#include "prometheus/ui/LocomotionEngine.hpp"

#include <algorithm>
#include <cmath>

namespace prometheus::ui {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

float sanitizeFinite(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

float clamp01(float value) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

void stepSpringAxis(float& value,
                    float& velocity,
                    float target,
                    float omega,
                    float dampingRatio,
                    float dt) noexcept {
    // Substep so high-dt frames remain unconditionally stable
    constexpr float kMaxSubDt = 0.016f;
    float remaining = dt;
    while (remaining > 0.0f) {
        const float h = std::min(remaining, kMaxSubDt);
        const float accel = omega * omega * (target - value) - 2.0f * dampingRatio * omega * velocity;
        velocity += accel * h;
        value += velocity * h;
        remaining -= h;
    }
}

} // namespace

const char* locomotionModeToString(LocomotionMode mode) noexcept {
    switch (mode) {
        case LocomotionMode::Stationary:    return "Stationary";
        case LocomotionMode::IdleWander:    return "IdleWander";
        case LocomotionMode::ExplorePatrol: return "ExplorePatrol";
        case LocomotionMode::ObserveGaze:   return "ObserveGaze";
        case LocomotionMode::Dragging:      return "Dragging";
        case LocomotionMode::EdgePeek:      return "EdgePeek";
        default:                            return "Stationary";
    }
}

LocomotionEngine::LocomotionEngine(uint32_t seed, LocomotionConfig config) noexcept
    : cfg_(config)
    , rng_(seed)
{
    reset(seed);
}

void LocomotionEngine::reset(uint32_t seed) noexcept {
    rng_.seed(seed);
    out_ = LocomotionOutput{};

    walking_ = false;
    walkDir_ = 1;
    forcedNextDir_ = 0;
    remainingWalkPx_ = 0.0f;
    activeSpeedPxPerSec_ = 0.0f;
    walkCooldownSec_ = randRange(1.2f, 2.8f); // initial, to be tuned
    walkPhase_ = 0.0f;
    walkIntensity_ = 0.0f;
    facingDir_ = 1;
    hasVirtualX_ = false;
    virtualWinX_ = 0.0f;
    lastAppliedWinX_ = 0.0f;

    wasDragging_ = false;
    prevWinX_ = 0.0f;
    prevWinY_ = 0.0f;
    filteredVx_ = 0.0f;
    filteredVy_ = 0.0f;
    swayRot_ = 0.0f;
    swayRotVel_ = 0.0f;
    swayOffX_ = 0.0f;
    swayOffXVel_ = 0.0f;
    swayOffY_ = 0.0f;
    swayOffYVel_ = 0.0f;

    edgePeekSide_ = 0;
    edgeCooldownSec_ = 0.0f;
    gazeLean_ = 0.0f;
    gazeShiftX_ = 0.0f;
}

float LocomotionEngine::randUnit() noexcept {
    constexpr float kInvMax = 1.0f / static_cast<float>(std::mt19937::max());
    return static_cast<float>(rng_()) * kInvMax;
}

float LocomotionEngine::randRange(float minVal, float maxVal) noexcept {
    if (maxVal <= minVal) return minVal;
    return minVal + randUnit() * (maxVal - minVal);
}

void LocomotionEngine::scheduleNextWalk(common::Behaviour behaviour,
                                        common::QualityTier tier,
                                        bool initial) noexcept {
    if (initial) {
        walkCooldownSec_ = randRange(0.8f, 1.8f); // initial, to be tuned
        return;
    }

    if (behaviour == common::Behaviour::Exploring) {
        const float pauseScale = (static_cast<int>(tier) >= 3) ? 1.0f : cfg_.exploreLitePauseScale;
        walkCooldownSec_ = randRange(cfg_.explorePauseMinSec, cfg_.explorePauseMaxSec) * pauseScale;
    } else {
        walkCooldownSec_ = randRange(cfg_.idlePauseMinSec, cfg_.idlePauseMaxSec);
    }
}

void LocomotionEngine::startWalk(common::Behaviour behaviour,
                                 common::QualityTier tier,
                                 float curiosity,
                                 float energy,
                                 float windowX,
                                 float minWinX,
                                 float maxWinX) noexcept {
    int chosenDir = 0;
    if (forcedNextDir_ != 0) {
        chosenDir = forcedNextDir_;
        forcedNextDir_ = 0;
    } else {
        // Mate-Engine PickDirectionByEdges analog: turn away when close to screen border
        const float leftDist = windowX - minWinX;
        const float rightDist = maxWinX - windowX;
        if (leftDist <= cfg_.edgeTurnThresholdPx && rightDist <= cfg_.edgeTurnThresholdPx) {
            chosenDir = (leftDist < rightDist) ? 1 : ((rightDist < leftDist) ? -1 : ((randUnit() < 0.5f) ? -1 : 1));
        } else if (rightDist <= cfg_.edgeTurnThresholdPx) {
            chosenDir = -1;
        } else if (leftDist <= cfg_.edgeTurnThresholdPx) {
            chosenDir = 1;
        } else {
            chosenDir = (randUnit() < 0.5f) ? -1 : 1;
        }
    }

    float minDist = cfg_.idleWalkMinPx;
    float maxDist = cfg_.idleWalkMaxPx;
    float speed = cfg_.idleSpeedPxPerSec;

    if (behaviour == common::Behaviour::Exploring) {
        const bool fullExplore = static_cast<int>(tier) >= 3;
        const float distScale = fullExplore ? 1.0f : cfg_.exploreLiteDistScale;
        const float speedScale = fullExplore ? 1.0f : cfg_.exploreLiteSpeedScale;
        minDist = cfg_.exploreWalkMinPx * distScale;
        maxDist = cfg_.exploreWalkMaxPx * distScale;
        speed = (cfg_.exploreBaseSpeedPxPerSec + curiosity * 35.0f + energy * 20.0f) * speedScale;
    } else {
        // Idle walk scales gently with energy
        speed = cfg_.idleSpeedPxPerSec * (0.85f + 0.30f * energy);
    }

    // Clamp walk distance to available travel in chosenDir when close to edge
    const float roomInDir = (chosenDir > 0) ? std::max(0.0f, maxWinX - windowX) : std::max(0.0f, windowX - minWinX);
    if (roomInDir < 12.0f) {
        chosenDir = -chosenDir;
    }

    const float chosenDist = randRange(minDist, maxDist);
    if (chosenDist < 1.0f || speed <= 0.5f) {
        scheduleNextWalk(behaviour, tier, false);
        return;
    }

    walkDir_ = chosenDir;
    facingDir_ = chosenDir;
    remainingWalkPx_ = chosenDist;
    activeSpeedPxPerSec_ = speed;
    walking_ = true;
}

void LocomotionEngine::stopWalk() noexcept {
    walking_ = false;
    remainingWalkPx_ = 0.0f;
}

void LocomotionEngine::updateSwaySpring(float dt,
                                        bool highFidelity,
                                        float targetRot,
                                        float targetOffX,
                                        float targetOffY) noexcept {
    if (dt <= 0.0f) return;

    if (highFidelity) {
        const float omega = kTwoPi * std::max(0.1f, cfg_.swaySpringFreqHz);
        stepSpringAxis(swayRot_, swayRotVel_, targetRot, omega, cfg_.swayDampingRatio, dt);
        stepSpringAxis(swayOffX_, swayOffXVel_, targetOffX, omega, cfg_.swayDampingRatio, dt);
        stepSpringAxis(swayOffY_, swayOffYVel_, targetOffY, omega, cfg_.swayDampingRatio, dt);
    } else {
        // Lower-tier exponential settle without spring oscillation
        const float alpha = 1.0f - std::exp(-12.0f * dt);
        swayRot_ += (targetRot - swayRot_) * alpha;
        swayOffX_ += (targetOffX - swayOffX_) * alpha;
        swayOffY_ += (targetOffY - swayOffY_) * alpha;
        swayRotVel_ = 0.0f;
        swayOffXVel_ = 0.0f;
        swayOffYVel_ = 0.0f;
    }
}

LocomotionOutput LocomotionEngine::step(const LocomotionInput& in) noexcept {
    const float dt = std::clamp(sanitizeFinite(in.dtSeconds, 0.0f), 0.0f, 0.1f);
    const float energy = clamp01(in.energy);
    const float curiosity = clamp01(in.curiosity);
    const float windowX = sanitizeFinite(in.windowX, 0.0f);
    const float windowY = sanitizeFinite(in.windowY, 0.0f);
    const float stageW = std::max(1.0f, sanitizeFinite(in.stageWidth, 280.0f));
    const float stageH = std::max(1.0f, sanitizeFinite(in.stageHeight, 300.0f));
    const float charBottomY = sanitizeFinite(in.charBottomOffsetY, 185.0f);
    const float workLeft = sanitizeFinite(in.workLeft, 0.0f);
    const float workTop = sanitizeFinite(in.workTop, 0.0f);
    const float workW = std::max(1.0f, sanitizeFinite(in.workWidth, 1920.0f));
    const float workH = std::max(1.0f, sanitizeFinite(in.workHeight, 1040.0f));

    const float minWinX = workLeft;
    const float maxWinX = std::max(workLeft, workLeft + workW - stageW);
    const bool canWalkHorizontally = (maxWinX - minWinX) >= 8.0f;

    LocomotionOutput out{};
    out.targetWindowX = windowX;

    // 1. Hard dormancy gate: halt all motion and spring energy immediately
    if (in.dormant || in.tier == common::QualityTier::Tier0_Dormant) {
        stopWalk();
        activeSpeedPxPerSec_ = 0.0f;
        walkIntensity_ = 0.0f;
        walkPhase_ = 0.0f;
        hasVirtualX_ = false;
        filteredVx_ = 0.0f;
        filteredVy_ = 0.0f;
        swayRot_ = swayRotVel_ = 0.0f;
        swayOffX_ = swayOffXVel_ = 0.0f;
        swayOffY_ = swayOffYVel_ = 0.0f;
        gazeLean_ = 0.0f;
        gazeShiftX_ = 0.0f;
        edgePeekSide_ = 0;
        wasDragging_ = in.dragging;
        prevWinX_ = windowX;
        prevWinY_ = windowY;

        out.facingDir = facingDir_;
        out.mode = LocomotionMode::Stationary;
        out.needsContinuousTick = false;
        out_ = out;
        return out_;
    }

    if (!hasVirtualX_ || std::abs(windowX - lastAppliedWinX_) >= 2.0f) {
        virtualWinX_ = windowX;
        lastAppliedWinX_ = windowX;
        hasVirtualX_ = true;
    }

    if (edgeCooldownSec_ > 0.0f) {
        edgeCooldownSec_ = std::max(0.0f, edgeCooldownSec_ - dt);
    }

    // 2. Dragging + Edge-Peek snap hysteresis + Drag velocity spring
    float targetSwayRot = 0.0f;
    float targetSwayOffX = 0.0f;
    float targetSwayOffY = 0.0f;

    if (in.dragging) {
        if (wasDragging_ && dt > 1e-5f) {
            const float rawVx = std::clamp((windowX - prevWinX_) / dt, -2500.0f, 2500.0f);
            const float rawVy = std::clamp((windowY - prevWinY_) / dt, -2500.0f, 2500.0f);
            const float velAlpha = 1.0f - std::exp(-cfg_.swayVelFilterHz * dt);
            filteredVx_ += (rawVx - filteredVx_) * velAlpha;
            filteredVy_ += (rawVy - filteredVy_) * velAlpha;
        } else {
            filteredVx_ = 0.0f;
            filteredVy_ = 0.0f;
        }
        prevWinX_ = windowX;
        prevWinY_ = windowY;

        if (canWalkHorizontally) {
            if (edgePeekSide_ == 0) {
                if (edgeCooldownSec_ <= 0.0f) {
                    if (windowX <= minWinX + cfg_.edgeSnapThresholdPx) {
                        edgePeekSide_ = -1;
                        facingDir_ = 1;
                    } else if (windowX >= maxWinX - cfg_.edgeSnapThresholdPx) {
                        edgePeekSide_ = 1;
                        facingDir_ = -1;
                    }
                }
            } else if (edgePeekSide_ == -1) {
                if (windowX > minWinX + cfg_.edgeUnsnapThresholdPx) {
                    edgePeekSide_ = 0;
                    edgeCooldownSec_ = cfg_.edgeUnsnapCooldownSec;
                }
            } else if (edgePeekSide_ == 1) {
                if (windowX < maxWinX - cfg_.edgeUnsnapThresholdPx) {
                    edgePeekSide_ = 0;
                    edgeCooldownSec_ = cfg_.edgeUnsnapCooldownSec;
                }
            }
        } else {
            edgePeekSide_ = 0;
        }

        targetSwayRot = std::clamp(-filteredVx_ * cfg_.swayVelToRotGain,
                                   -cfg_.swayMaxRotDeg, cfg_.swayMaxRotDeg);
        targetSwayOffX = std::clamp(-filteredVx_ * cfg_.swayVelToOffsetXGain,
                                    -cfg_.swayMaxOffsetX, cfg_.swayMaxOffsetX);
        targetSwayOffY = std::clamp(-filteredVy_ * cfg_.swayVelToOffsetYGain,
                                    -cfg_.swayMaxOffsetY, cfg_.swayMaxOffsetY);

        stopWalk();
    } else {
        filteredVx_ = 0.0f;
        filteredVy_ = 0.0f;
        if (wasDragging_) {
            // Brief settle pause after user drops the organism
            walkCooldownSec_ = std::max(walkCooldownSec_, 0.6f);
        }
        if (!canWalkHorizontally) {
            edgePeekSide_ = 0;
        }
    }
    wasDragging_ = in.dragging;

    const bool highFidelitySpring = static_cast<int>(in.tier) >= 2;
    updateSwaySpring(dt, highFidelitySpring, targetSwayRot, targetSwayOffX, targetSwayOffY);

    // 3. Edge-Peek tuck glide when released near left/right screen edge
    bool edgeGlideActive = false;
    if (!in.dragging && edgePeekSide_ != 0) {
        const float targetPeekX = (edgePeekSide_ < 0)
            ? (minWinX - cfg_.edgeTuckPx)
            : (maxWinX + cfg_.edgeTuckPx);
        facingDir_ = (edgePeekSide_ < 0) ? 1 : -1;

        const float diff = targetPeekX - virtualWinX_;
        if (std::abs(diff) > 0.5f) {
            const float alpha = (dt > 0.0f) ? (1.0f - std::exp(-12.0f * dt)) : 1.0f;
            virtualWinX_ += diff * alpha;
            const float roundedX = std::round(virtualWinX_);
            out.targetWindowX = roundedX;
            out.shouldMoveWindow = (std::abs(roundedX - windowX) >= 0.5f);
            lastAppliedWinX_ = roundedX;
            edgeGlideActive = true;
        }
    }

    // 4. Taskbar / work-area bottom sitting detection (Mate-Engine AvatarTaskbarController analog)
    const float workBottom = workTop + workH;
    const float winBottomDist = std::abs((windowY + stageH) - workBottom);
    const float charBottomDist = std::abs((windowY + charBottomY) - workBottom);
    const bool nearTaskbar = (winBottomDist <= cfg_.taskbarSnapBandPx) ||
                             (charBottomDist <= cfg_.taskbarSnapBandPx);
    const bool isSitting = !in.dragging &&
                           (edgePeekSide_ == 0) &&
                           nearTaskbar &&
                           (in.behaviour == common::Behaviour::Idle ||
                            in.behaviour == common::Behaviour::Resting);

    // 5. Autonomous Desktop Walking (Mate-Engine AvatarLocomotionController analog)
    const bool behaviourAllowsWalk =
        (in.behaviour == common::Behaviour::Exploring) ||
        (in.behaviour == common::Behaviour::Idle && static_cast<int>(in.tier) >= 2);
    const bool canWalkNow = in.locomotionEnabled &&
                            !in.dragging &&
                            (edgePeekSide_ == 0) &&
                            !in.interactionBlocked &&
                            !isSitting &&
                            canWalkHorizontally &&
                            behaviourAllowsWalk;

    if (!canWalkNow) {
        if (walking_) {
            stopWalk();
            walkCooldownSec_ = std::max(walkCooldownSec_, 0.5f);
        }
    } else if (walking_) {
        const float stepPx = std::min(activeSpeedPxPerSec_ * dt, remainingWalkPx_);
        const float desiredX = virtualWinX_ + stepPx * static_cast<float>(walkDir_);
        const float clampedX = std::clamp(desiredX, minWinX, maxWinX);
        const float actualMoved = std::abs(clampedX - virtualWinX_);

        virtualWinX_ = clampedX;
        const float roundedX = std::round(virtualWinX_);
        out.targetWindowX = roundedX;
        out.shouldMoveWindow = (std::abs(roundedX - windowX) >= 0.5f);
        lastAppliedWinX_ = roundedX;
        remainingWalkPx_ = std::max(0.0f, remainingWalkPx_ - actualMoved);

        if (stepPx > 0.25f && actualMoved < stepPx * 0.25f) {
            // Reached monitor edge; turn around on next walk cycle
            forcedNextDir_ = -walkDir_;
            stopWalk();
            scheduleNextWalk(in.behaviour, in.tier, false);
        } else if (remainingWalkPx_ <= 0.5f) {
            stopWalk();
            scheduleNextWalk(in.behaviour, in.tier, false);
        }
    } else {
        walkCooldownSec_ -= dt;
        if (walkCooldownSec_ <= 0.0f) {
            startWalk(in.behaviour, in.tier, curiosity, energy, windowX, minWinX, maxWinX);
        }
    }

    // 6. Gait phase & envelope (ramps smoothly so sprite never pops on start/stop)
    if (walking_) {
        walkIntensity_ = std::min(1.0f, walkIntensity_ + cfg_.walkIntensityAttackPerSec * dt);
        const float stepHz = cfg_.walkStepBaseHz + activeSpeedPxPerSec_ * cfg_.walkStepSpeedFactor;
        walkPhase_ = std::fmod(walkPhase_ + kTwoPi * stepHz * dt, kTwoPi);
    } else {
        walkIntensity_ = std::max(0.0f, walkIntensity_ - cfg_.walkIntensityReleasePerSec * dt);
        if (walkIntensity_ <= 0.001f) {
            walkIntensity_ = 0.0f;
            walkPhase_ = 0.0f;
            activeSpeedPxPerSec_ = 0.0f;
        }
    }

    // 7. Cursor gaze lean (Mate-Engine AvatarMouseTracking analog)
    float targetGazeLean = 0.0f;
    float targetGazeShift = 0.0f;
    const bool allowGaze = !in.dragging &&
                           !walking_ &&
                           (edgePeekSide_ == 0) &&
                           in.cursorValid &&
                           static_cast<int>(in.tier) >= 2 &&
                           (in.behaviour == common::Behaviour::Observing ||
                            in.behaviour == common::Behaviour::Idle);
    if (allowGaze) {
        const float centerX = windowX + stageW * 0.5f;
        const float dx = sanitizeFinite(in.cursorX, centerX) - centerX;
        const float absDx = std::abs(dx);
        if (absDx > cfg_.gazeDeadzonePx) {
            const float sign = (dx >= 0.0f) ? 1.0f : -1.0f;
            const float strength = std::clamp((absDx - cfg_.gazeDeadzonePx) / 260.0f, 0.0f, 1.0f);
            const float stateGain = (in.behaviour == common::Behaviour::Observing) ? 1.0f : 0.55f;
            targetGazeLean = sign * strength * cfg_.gazeMaxRotDeg * stateGain;
            targetGazeShift = sign * strength * cfg_.gazeMaxShiftX * stateGain;
            if (in.behaviour == common::Behaviour::Observing && absDx > 48.0f) {
                facingDir_ = (dx >= 0.0f) ? 1 : -1;
            }
        }
    }
    if (dt > 0.0f) {
        const float gazeAlpha = 1.0f - std::exp(-cfg_.gazeSmoothHz * dt);
        gazeLean_ += (targetGazeLean - gazeLean_) * gazeAlpha;
        gazeShiftX_ += (targetGazeShift - gazeShiftX_) * gazeAlpha;
        if (std::abs(gazeLean_) < 0.01f && targetGazeLean == 0.0f) gazeLean_ = 0.0f;
        if (std::abs(gazeShiftX_) < 0.01f && targetGazeShift == 0.0f) gazeShiftX_ = 0.0f;
    }

    // 8. Mode & continuous-tick classification
    if (in.dragging) {
        out.mode = LocomotionMode::Dragging;
    } else if (edgePeekSide_ != 0) {
        out.mode = LocomotionMode::EdgePeek;
    } else if (walking_) {
        out.mode = (in.behaviour == common::Behaviour::Exploring)
            ? LocomotionMode::ExplorePatrol
            : LocomotionMode::IdleWander;
    } else if (in.behaviour == common::Behaviour::Observing && static_cast<int>(in.tier) >= 2) {
        out.mode = LocomotionMode::ObserveGaze;
    } else {
        out.mode = LocomotionMode::Stationary;
    }

    const bool swaySettling = std::abs(swayRot_) > 0.05f ||
                              std::abs(swayRotVel_) > 0.25f ||
                              std::abs(swayOffX_) > 0.10f ||
                              std::abs(swayOffXVel_) > 0.25f ||
                              std::abs(swayOffY_) > 0.10f ||
                              std::abs(swayOffYVel_) > 0.25f;
    const bool gazeSettling = (targetGazeLean != 0.0f) ||
                              (std::abs(gazeLean_) > 0.05f) ||
                              (std::abs(gazeShiftX_) > 0.05f);

    out.isWalking = walking_;
    out.facingDir = facingDir_;
    out.walkPhase = walkPhase_;
    out.walkIntensity = walkIntensity_;
    out.walkSpeedPxPerSec = walking_ ? activeSpeedPxPerSec_ : 0.0f;
    out.swayRotationDeg = swayRot_;
    out.swayOffsetX = swayOffX_;
    out.swayOffsetY = swayOffY_;
    out.gazeLeanDeg = gazeLean_;
    out.gazeShiftX = gazeShiftX_;
    out.edgePeekSide = edgePeekSide_;
    out.isSitting = isSitting;
    out.needsContinuousTick = in.dragging ||
                              walking_ ||
                              (walkIntensity_ > 0.001f) ||
                              swaySettling ||
                              edgeGlideActive ||
                              (allowGaze && (gazeSettling || in.behaviour == common::Behaviour::Observing)) ||
                              (!allowGaze && gazeSettling);

    out_ = out;
    return out_;
}

} // namespace prometheus::ui
