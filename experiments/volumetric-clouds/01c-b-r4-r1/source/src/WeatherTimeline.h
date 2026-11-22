#pragma once

// V-WEATHER-01B B2A: developer-only weather timeline plan and deterministic
// visual-state evaluation. The evaluator only selects modes and mixtures; it
// never creates a new density model or changes seeds.

#include "CloudTypes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace clouds
{
enum class WeatherTimelineKind
{
    Invalid = 0,
    Legacy,
    LegacyInterrupted,
    TwoState,
    ThreeState,
};

struct WeatherTimelinePlan
{
    std::vector<StaticWeatherMode> modes;
    float segmentDuration = 2.0f;
    bool hasInterrupt = false;
    int interruptSegment = 0;
    float interruptPhase = 0.0f;
    StaticWeatherMode interruptTarget = StaticWeatherMode::Overcast;
    float interruptDuration = 2.0f;
};

struct WeatherTimelineVisual
{
    WeatherTimelineKind kind = WeatherTimelineKind::Invalid;
    StaticWeatherMode source = StaticWeatherMode::Clear;
    StaticWeatherMode target = StaticWeatherMode::Clear;
    StaticWeatherMode outer = StaticWeatherMode::Clear;
    float sourceMix = 0.0f;
    float outerMix = 0.0f;
    float phase = 0.0f;
    float basePhase = 0.0f;
    int segment = 0;
};

inline float WeatherTimelineCurve(float phase)
{
    const float p = std::clamp(phase, 0.0f, 1.0f);
    return p * p * (3.0f - 2.0f * p);
}

inline bool WeatherTimelineLegacyMode(StaticWeatherMode mode)
{
    return !StaticWeatherModeUsesStormFront(mode);
}

// B2A-R1: the animated clock and the fixed-time evaluator must agree on when
// the timeline actually ends. These helpers are the single source of truth for
// plan validity, the base sequence duration and the interrupt extension.
inline bool WeatherTimelinePlanValid(const WeatherTimelinePlan& plan)
{
    return plan.modes.size() >= 2 &&
           std::isfinite(plan.segmentDuration) &&
           plan.segmentDuration > 0.0f;
}

inline int WeatherTimelineSegmentCount(const WeatherTimelinePlan& plan)
{
    return WeatherTimelinePlanValid(plan)
        ? static_cast<int>(plan.modes.size()) - 1 : 0;
}

inline bool WeatherTimelineInterruptValid(const WeatherTimelinePlan& plan)
{
    if (!plan.hasInterrupt || !WeatherTimelinePlanValid(plan))
        return false;
    const int segmentCount = WeatherTimelineSegmentCount(plan);
    return std::isfinite(plan.interruptPhase) &&
           plan.interruptPhase >= 0.0f &&
           plan.interruptPhase <= 1.0f &&
           plan.interruptSegment >= 0 &&
           plan.interruptSegment < segmentCount &&
           std::isfinite(plan.interruptDuration) &&
           plan.interruptDuration > 0.0f;
}

inline float WeatherTimelineBaseEnd(const WeatherTimelinePlan& plan)
{
    if (!WeatherTimelinePlanValid(plan))
        return 0.0f;
    return plan.segmentDuration *
           static_cast<float>(WeatherTimelineSegmentCount(plan));
}

inline float WeatherTimelineInterruptStart(const WeatherTimelinePlan& plan)
{
    if (!WeatherTimelineInterruptValid(plan))
        return 0.0f;
    return plan.segmentDuration *
               static_cast<float>(plan.interruptSegment) +
           plan.segmentDuration * plan.interruptPhase;
}

inline float WeatherTimelineEffectiveEnd(const WeatherTimelinePlan& plan)
{
    const float baseEnd = WeatherTimelineBaseEnd(plan);
    if (baseEnd <= 0.0f)
        return 0.0f;
    if (!WeatherTimelineInterruptValid(plan))
        return baseEnd;
    return std::max(
        baseEnd, WeatherTimelineInterruptStart(plan) + plan.interruptDuration);
}

inline float WeatherTimelineClampTime(
    const WeatherTimelinePlan& plan, float timeSeconds)
{
    const float end = WeatherTimelineEffectiveEnd(plan);
    if (end <= 0.0f)
        return 0.0f;
    const float time = std::isfinite(timeSeconds)
        ? std::max(timeSeconds, 0.0f) : 0.0f;
    return std::min(time, end);
}

inline WeatherTimelineVisual EvaluateWeatherTimeline(
    const WeatherTimelinePlan& plan, float timeSeconds)
{
    WeatherTimelineVisual visual;
    if (!WeatherTimelinePlanValid(plan))
        return visual;

    const int segmentCount = WeatherTimelineSegmentCount(plan);
    const float duration = plan.segmentDuration;
    const float time = WeatherTimelineClampTime(plan, timeSeconds);

    if (WeatherTimelineInterruptValid(plan))
    {
        const int segment = plan.interruptSegment;
        const float interruptPhase = plan.interruptPhase;
        const float interruptTime = WeatherTimelineInterruptStart(plan);
        if (time >= interruptTime)
        {
            const float outerDuration = plan.interruptDuration;
            const float outerPhase = std::clamp(
                (time - interruptTime) / outerDuration, 0.0f, 1.0f);
            const float baseMix = WeatherTimelineCurve(interruptPhase);
            const float outerMix = WeatherTimelineCurve(outerPhase);
            const StaticWeatherMode baseSource = plan.modes[segment];
            const StaticWeatherMode baseTarget = plan.modes[segment + 1];

            visual.segment = segment;
            visual.source = baseSource;
            visual.target = baseTarget;
            visual.sourceMix = baseMix;
            visual.outerMix = outerMix;
            visual.phase = outerPhase;
            visual.basePhase = interruptPhase;

            if (WeatherTimelineLegacyMode(baseSource) &&
                WeatherTimelineLegacyMode(baseTarget))
            {
                // The current legacy visual state can be represented by the
                // single interpolated CloudParams; retarget it directly.
                visual.kind = WeatherTimelineKind::LegacyInterrupted;
                visual.outer = plan.interruptTarget;
                return visual;
            }

            // SF-involving transitions keep the current two-state pair as an
            // inner blend. Reversing/continuing within that pair stays
            // two-state; a third target uses the minimal three-state contract.
            if (plan.interruptTarget == baseSource)
            {
                visual.kind = WeatherTimelineKind::TwoState;
                visual.sourceMix = baseMix * (1.0f - outerMix);
                return visual;
            }
            if (plan.interruptTarget == baseTarget)
            {
                visual.kind = WeatherTimelineKind::TwoState;
                visual.sourceMix = baseMix +
                    (1.0f - baseMix) * outerMix;
                return visual;
            }
            visual.kind = WeatherTimelineKind::ThreeState;
            visual.outer = plan.interruptTarget;
            return visual;
        }
    }

    int segment = static_cast<int>(std::floor(time / duration));
    segment = std::clamp(segment, 0, segmentCount - 1);
    const float phase = std::clamp(
        (time - duration * static_cast<float>(segment)) / duration,
        0.0f, 1.0f);
    visual.kind = (WeatherTimelineLegacyMode(plan.modes[segment]) &&
                   WeatherTimelineLegacyMode(plan.modes[segment + 1]))
        ? WeatherTimelineKind::Legacy : WeatherTimelineKind::TwoState;
    visual.segment = segment;
    visual.source = plan.modes[segment];
    visual.target = plan.modes[segment + 1];
    visual.sourceMix = WeatherTimelineCurve(phase);
    visual.phase = phase;
    visual.basePhase = phase;
    return visual;
}
} // namespace clouds
