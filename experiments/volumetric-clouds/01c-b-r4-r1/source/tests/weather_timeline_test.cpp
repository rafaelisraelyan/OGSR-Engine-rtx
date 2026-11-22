#include "WeatherTimeline.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
}

void ExpectNear(float actual, float expected, const char* message,
                float tolerance = 1.0e-5f)
{
    if (std::fabs(actual - expected) > tolerance)
    {
        std::printf("FAIL: %s (actual %.9g expected %.9g)\n",
                    message, actual, expected);
        ++g_failures;
    }
}

clouds::WeatherTimelinePlan MakePlan(
    std::initializer_list<clouds::StaticWeatherMode> modes, float duration)
{
    clouds::WeatherTimelinePlan plan;
    plan.modes.assign(modes.begin(), modes.end());
    plan.segmentDuration = duration;
    return plan;
}
} // namespace

int main()
{
    using namespace clouds;

    // Uninterrupted canonical scenario A: four segments of 0.5 s.
    {
        WeatherTimelinePlan plan = MakePlan(
            { StaticWeatherMode::Clear, StaticWeatherMode::Partly,
              StaticWeatherMode::Overcast, StaticWeatherMode::RainSF,
              StaticWeatherMode::Partly }, 0.5f);
        Expect(WeatherTimelinePlanValid(plan), "scenario A plan is valid");
        ExpectNear(WeatherTimelineBaseEnd(plan), 2.0f,
                   "scenario A base end");
        ExpectNear(WeatherTimelineEffectiveEnd(plan), 2.0f,
                   "scenario A effective end without interrupt");
        ExpectNear(WeatherTimelineClampTime(plan, 99.0f), 2.0f,
                   "scenario A clamps past the end");

        const WeatherTimelineVisual start =
            EvaluateWeatherTimeline(plan, 0.0f);
        Expect(start.kind == WeatherTimelineKind::Legacy,
               "scenario A starts in the legacy path");
        Expect(start.source == StaticWeatherMode::Clear &&
                   start.target == StaticWeatherMode::Partly,
               "scenario A first segment modes");
        ExpectNear(start.sourceMix, 0.0f, "scenario A first mix");

        const WeatherTimelineVisual end =
            EvaluateWeatherTimeline(plan, 2.0f);
        Expect(end.kind == WeatherTimelineKind::TwoState,
               "scenario A final SF segment uses two-state");
        Expect(end.source == StaticWeatherMode::RainSF &&
                   end.target == StaticWeatherMode::Partly,
               "scenario A final segment modes");
        ExpectNear(end.sourceMix, 1.0f, "scenario A final mix");
    }

    // Canonical live i65: 65% of 0.5 s, then 0.5 s to Partly.
    {
        WeatherTimelinePlan plan = MakePlan(
            { StaticWeatherMode::Overcast, StaticWeatherMode::RainSF }, 0.5f);
        plan.hasInterrupt = true;
        plan.interruptSegment = 0;
        plan.interruptPhase = 0.65f;
        plan.interruptTarget = StaticWeatherMode::Partly;
        plan.interruptDuration = 0.5f;

        Expect(WeatherTimelineInterruptValid(plan), "i65 interrupt is valid");
        ExpectNear(WeatherTimelineBaseEnd(plan), 0.5f, "i65 base end");
        ExpectNear(WeatherTimelineInterruptStart(plan), 0.325f,
                   "i65 interrupt start");
        ExpectNear(WeatherTimelineEffectiveEnd(plan), 0.825f,
                   "i65 effective end extends the sequence");

        const WeatherTimelineVisual before =
            EvaluateWeatherTimeline(plan, 0.324999f);
        ExpectNear(before.outerMix, 0.0f, "i65 pre-interrupt outer mix");
        const float baseMix = WeatherTimelineCurve(0.65f);
        ExpectNear(before.sourceMix, baseMix, "i65 pre-interrupt inner mix");

        const WeatherTimelineVisual start =
            EvaluateWeatherTimeline(plan, 0.325f);
        ExpectNear(start.outerMix, 0.0f, "i65 interrupt starts at zero");
        ExpectNear(start.sourceMix, baseMix, "i65 start preserves inner mix");

        const WeatherTimelineVisual middle =
            EvaluateWeatherTimeline(plan, 0.575f);
        ExpectNear(middle.outerMix, 0.5f, "i65 midpoint outer mix");
        ExpectNear(middle.sourceMix, baseMix, "i65 midpoint inner mix");

        const WeatherTimelineVisual end =
            EvaluateWeatherTimeline(plan, 0.825f);
        ExpectNear(end.outerMix, 1.0f, "i65 reaches the new target");
        Expect(end.outerMix == 1.0f, "i65 outer mix is exactly one at end");
        Expect(end.kind == WeatherTimelineKind::ThreeState,
               "i65 target uses the three-state contract");

        const WeatherTimelineVisual after =
            EvaluateWeatherTimeline(plan, 5.0f);
        ExpectNear(after.outerMix, 1.0f,
                   "i65 stays at the new target after the end");
        ExpectNear(WeatherTimelineClampTime(plan, 5.0f), 0.825f,
                   "i65 clamps to the extended end");
    }

    // i35 returns to the source and still completes without a jump.
    {
        WeatherTimelinePlan plan = MakePlan(
            { StaticWeatherMode::Overcast, StaticWeatherMode::RainSF }, 1.0f);
        plan.hasInterrupt = true;
        plan.interruptSegment = 0;
        plan.interruptPhase = 0.35f;
        plan.interruptTarget = StaticWeatherMode::Overcast;
        plan.interruptDuration = 1.0f;

        ExpectNear(WeatherTimelineInterruptStart(plan), 0.35f,
                   "i35 interrupt start");
        ExpectNear(WeatherTimelineEffectiveEnd(plan), 1.35f,
                   "i35 effective end extends the sequence");

        const WeatherTimelineVisual pre =
            EvaluateWeatherTimeline(plan, 0.349999f);
        Expect(pre.kind == WeatherTimelineKind::TwoState,
               "i35 pre state is two-state");
        const WeatherTimelineVisual post =
            EvaluateWeatherTimeline(plan, 0.350001f);
        Expect(post.kind == WeatherTimelineKind::TwoState,
               "i35 post state is two-state");
        ExpectNear(post.outerMix, 0.0f, "i35 post starts at the current mix");

        const WeatherTimelineVisual end =
            EvaluateWeatherTimeline(plan, 1.35f);
        ExpectNear(end.outerMix, 1.0f, "i35 reaches source again");
        ExpectNear(end.sourceMix, 0.0f, "i35 final source mix is zero");
    }

    // Late interrupt in the last segment of a four-mode timeline.
    {
        WeatherTimelinePlan plan = MakePlan(
            { StaticWeatherMode::Clear, StaticWeatherMode::Partly,
              StaticWeatherMode::Overcast, StaticWeatherMode::RainSF }, 0.5f);
        plan.hasInterrupt = true;
        plan.interruptSegment = 2;
        plan.interruptPhase = 0.65f;
        plan.interruptTarget = StaticWeatherMode::Partly;
        plan.interruptDuration = 0.5f;

        ExpectNear(WeatherTimelineBaseEnd(plan), 1.5f,
                   "late interrupt base end");
        ExpectNear(WeatherTimelineInterruptStart(plan), 1.325f,
                   "late interrupt start");
        ExpectNear(WeatherTimelineEffectiveEnd(plan), 1.825f,
                   "late interrupt extends past the original total");
        Expect(WeatherTimelineEffectiveEnd(plan) >
                   WeatherTimelineBaseEnd(plan),
               "late interrupt effective end exceeds base end");

        const WeatherTimelineVisual end =
            EvaluateWeatherTimeline(plan, 1.825f);
        ExpectNear(end.outerMix, 1.0f,
                   "late interrupt reaches the new target");
        Expect(end.source == StaticWeatherMode::Overcast &&
                   end.target == StaticWeatherMode::RainSF &&
                   end.outer == StaticWeatherMode::Partly,
               "late interrupt keeps source/target/outer modes");
    }

    // Invalid plans are safe: zero is returned for duration, invalid
    // interrupts are ignored, and non-finite timestamps clamp to zero.
    {
        WeatherTimelinePlan oneMode = MakePlan(
            { StaticWeatherMode::Clear }, 1.0f);
        Expect(!WeatherTimelinePlanValid(oneMode),
               "one-mode plan is invalid");
        ExpectNear(WeatherTimelineEffectiveEnd(oneMode), 0.0f,
                   "invalid plan effective end is zero");
        Expect(EvaluateWeatherTimeline(oneMode, 1.0f).kind ==
                   WeatherTimelineKind::Invalid,
               "invalid plan evaluates as invalid");

        WeatherTimelinePlan zeroDuration = MakePlan(
            { StaticWeatherMode::Clear, StaticWeatherMode::Partly }, 0.0f);
        Expect(!WeatherTimelinePlanValid(zeroDuration),
               "zero-duration plan is invalid");
        ExpectNear(WeatherTimelineEffectiveEnd(zeroDuration), 0.0f,
                   "zero-duration effective end is zero");

        WeatherTimelinePlan badSegment = MakePlan(
            { StaticWeatherMode::Clear, StaticWeatherMode::Partly }, 1.0f);
        badSegment.hasInterrupt = true;
        badSegment.interruptSegment = 2;
        badSegment.interruptPhase = 0.5f;
        badSegment.interruptDuration = 1.0f;
        Expect(!WeatherTimelineInterruptValid(badSegment),
               "out-of-range interrupt segment is invalid");
        ExpectNear(WeatherTimelineEffectiveEnd(badSegment), 1.0f,
                   "invalid interrupt leaves the base end");

        WeatherTimelinePlan zeroInterrupt = MakePlan(
            { StaticWeatherMode::Clear, StaticWeatherMode::Partly }, 1.0f);
        zeroInterrupt.hasInterrupt = true;
        zeroInterrupt.interruptPhase = 0.5f;
        zeroInterrupt.interruptDuration = 0.0f;
        Expect(!WeatherTimelineInterruptValid(zeroInterrupt),
               "zero interrupt duration is invalid");
        ExpectNear(WeatherTimelineEffectiveEnd(zeroInterrupt), 1.0f,
                   "zero interrupt duration leaves the base end");

        WeatherTimelinePlan valid = MakePlan(
            { StaticWeatherMode::Clear, StaticWeatherMode::Partly }, 1.0f);
        const float nan = std::nanf("");
        ExpectNear(WeatherTimelineClampTime(valid, nan), 0.0f,
                   "non-finite time clamps to zero");
        ExpectNear(WeatherTimelineClampTime(valid, -3.0f), 0.0f,
                   "negative time clamps to zero");
    }

    if (g_failures != 0)
    {
        std::printf("WEATHER TIMELINE TESTS: FAIL (%d)\n", g_failures);
        return 1;
    }

    std::printf("WEATHER TIMELINE TESTS: PASS\n");
    return 0;
}
