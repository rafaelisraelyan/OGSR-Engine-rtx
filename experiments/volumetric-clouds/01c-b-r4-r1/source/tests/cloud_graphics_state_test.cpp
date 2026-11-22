#include "CloudGraphicsState.h"

#include <cstdio>

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
} // namespace

int main()
{
    using namespace clouds;

    const CloudGraphicsState initial{
        true, CloudQuality::Quality, CloudRenderMode::Volumetric3D};
    CloudGraphicsStateMachine machine;
    machine.Initialize(initial);
    Expect(machine.Generation() == 0, "initial generation");
    Expect(!machine.HasPending(), "no pending initially");
    Expect(machine.Effective() == initial, "effective initial state");

    // 1. Quality -> Reference -> Quality cancels before a frame boundary.
    {
        CloudGraphicsState reference = initial;
        reference.quality = CloudQuality::Reference;
        Expect(machine.Request(reference) == CloudSettingsRequest::Accepted,
               "reference accepted");
        Expect(machine.HasPending(), "reference pending");
        Expect(machine.Effective().quality == CloudQuality::Reference,
               "effective pending reference");
        Expect(machine.Request(initial) == CloudSettingsRequest::Cancelled,
               "return to current cancels pending");
        Expect(!machine.HasPending(), "cancel clears pending");
        Expect(machine.Commit() == 0, "cancelled request does not bump generation");
        Expect(machine.Current().quality == CloudQuality::Quality,
               "current remains quality");
    }

    // 2. Quality -> Performance -> Balanced, one commit.
    {
        CloudGraphicsState performance = initial;
        performance.quality = CloudQuality::Performance;
        Expect(machine.Request(performance) == CloudSettingsRequest::Accepted,
               "performance accepted");
        CloudGraphicsState balanced = machine.Effective();
        balanced.quality = CloudQuality::Balanced;
        Expect(machine.Request(balanced) == CloudSettingsRequest::Accepted,
               "balanced replaces pending performance");
        Expect(machine.Pending().quality == CloudQuality::Balanced,
               "pending balanced");
        Expect(machine.Commit() == 1, "one commit for merged requests");
        Expect(machine.Current().quality == CloudQuality::Balanced,
               "current balanced");
    }

    // 3. Two clouds toggles before a frame return to the original state.
    {
        CloudGraphicsState off = machine.Effective();
        off.cloudsEnabled = false;
        Expect(machine.Request(off) == CloudSettingsRequest::Accepted,
               "clouds off accepted");
        CloudGraphicsState on = machine.Effective();
        on.cloudsEnabled = true;
        Expect(machine.Request(on) == CloudSettingsRequest::Cancelled,
               "second C cancels off");
        Expect(!machine.HasPending(), "toggle cancel clears pending");
        Expect(machine.Commit() == 1, "toggle cancel does not bump generation");
        Expect(machine.Current().cloudsEnabled, "clouds remain on");
    }

    // 4. Two F6 before a frame apply two quality steps.
    {
        CloudGraphicsState first = machine.Effective();
        first.quality = CloudQuality::Quality;
        Expect(machine.Request(first) == CloudSettingsRequest::Accepted,
               "first F6 accepted");
        CloudGraphicsState second = machine.Effective();
        second.quality = CloudQuality::Reference;
        Expect(machine.Request(second) == CloudSettingsRequest::Accepted,
               "second F6 accepted");
        Expect(machine.Commit() == 2, "two steps need one new generation");
        Expect(machine.Current().quality == CloudQuality::Reference,
               "two F6 reaches reference");
    }

    // 5. F6 then Shift+F6 cancels before a frame.
    {
        CloudGraphicsState forward = machine.Effective();
        forward.quality = CloudQuality::Performance;
        Expect(machine.Request(forward) == CloudSettingsRequest::Accepted,
               "F6 accepted");
        CloudGraphicsState backward = machine.Effective();
        backward.quality = CloudQuality::Reference;
        Expect(machine.Request(backward) == CloudSettingsRequest::Cancelled,
               "Shift+F6 cancels");
        Expect(!machine.HasPending(), "reverse cancel clears pending");
        Expect(machine.Commit() == 2, "reverse cancel no generation");
        Expect(machine.Current().quality == CloudQuality::Reference,
               "reverse returns to current reference");
    }

    // 6. Unsupported classic2d leaves an existing pending request intact.
    {
        CloudGraphicsState pending = machine.Effective();
        pending.quality = CloudQuality::Quality;
        Expect(machine.Request(pending) == CloudSettingsRequest::Accepted,
               "pending quality accepted");
        CloudGraphicsState classic = machine.Effective();
        classic.renderMode = CloudRenderMode::Classic2D;
        Expect(machine.Request(classic) == CloudSettingsRequest::Unsupported,
               "classic2d unsupported");
        Expect(machine.HasPending(), "unsupported keeps pending");
        Expect(machine.Pending().quality == CloudQuality::Quality,
               "unsupported does not replace pending");
        Expect(machine.Commit() == 3, "pending still commits");
        Expect(machine.Current().quality == CloudQuality::Quality,
               "pending quality committed");
    }

    // 7. Repeated identical pending request does not add generation work.
    {
        CloudGraphicsState next = machine.Effective();
        next.quality = CloudQuality::Balanced;
        Expect(machine.Request(next) == CloudSettingsRequest::Accepted,
               "pending balanced accepted");
        Expect(machine.Request(next) == CloudSettingsRequest::NoChange,
               "repeat pending is no-change");
        Expect(machine.Commit() == 4, "repeat has one generation increment");
        Expect(machine.Commit() == 4, "commit with no pending is stable");
        Expect(machine.Generation() == 4, "generation after empty commit");
    }

    // 8. Reinitialize resets current/pending/generation.
    {
        machine.Initialize(initial);
        Expect(machine.Generation() == 0, "reinit generation reset");
        Expect(!machine.HasPending(), "reinit pending cleared");
        Expect(machine.Current() == initial, "reinit current reset");
        Expect(machine.Effective() == initial, "reinit effective reset");
    }

    // Mapping contract.
    const CloudQualityProfile performance =
        GetCloudQualityProfile(CloudQuality::Performance);
    Expect(performance.raySteps == 64 && performance.shadowSteps == 2 &&
           performance.outputDivisor == 2, "performance mapping");
    const CloudQualityProfile balanced =
        GetCloudQualityProfile(CloudQuality::Balanced);
    Expect(balanced.raySteps == 96 && balanced.shadowSteps == 4 &&
           balanced.outputDivisor == 2, "balanced mapping");
    const CloudQualityProfile quality =
        GetCloudQualityProfile(CloudQuality::Quality);
    Expect(quality.raySteps == 128 && quality.shadowSteps == 6 &&
           quality.outputDivisor == 2, "quality mapping");
    const CloudQualityProfile referenceProfile =
        GetCloudQualityProfile(CloudQuality::Reference);
    Expect(referenceProfile.raySteps == 128 &&
           referenceProfile.shadowSteps == 6 &&
           referenceProfile.outputDivisor == 1, "reference mapping");

    if (g_failures != 0)
    {
        std::printf("CLOUD GRAPHICS STATE TESTS: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("CLOUD GRAPHICS STATE TESTS: PASS\n");
    return 0;
}
