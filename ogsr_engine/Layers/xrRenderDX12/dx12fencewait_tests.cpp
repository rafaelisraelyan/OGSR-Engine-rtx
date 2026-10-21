#include "dx12fencewait.h"
#include "dx12lifetime.h"
#include "dx12timing.h"
#include <array>

#include <cstdint>
#include <cstdio>

namespace dx12fence12
{
namespace
{
struct StubFence
{
    uint64_t before = 0;
    uint64_t after = 0;
    WaitStatus waitStatus = WaitStatus::Signaled;
    bool armResult = true;
    int armCalls = 0;
    int waitCalls = 0;

    uint64_t GetCompletedValue()
    {
        return waitCalls == 0 ? before : after;
    }

    bool SetEventOnCompletion(uint64_t)
    {
        ++armCalls;
        return armResult;
    }

    WaitStatus Wait(uint32_t)
    {
        ++waitCalls;
        return waitStatus;
    }
};

struct TestState
{
    unsigned checks = 0;
    unsigned failures = 0;

    void Expect(bool condition, const char* name)
    {
        ++checks;
        if (!condition)
        {
            ++failures;
            std::printf("[fencewait] FAIL: %s\n", name);
        }
    }
};

void TestCompletedAndAlreadyComplete(TestState& test)
{
    {
        StubFence fence;
        fence.before = 6;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::Completed, "already complete outcome");
        test.Expect(fence.armCalls == 0, "already complete must not arm");
        test.Expect(fence.waitCalls == 0, "already complete must not wait");
    }
    {
        StubFence fence;
        fence.before = 0;
        const Result result = WaitForValue(fence, 0, 5000);
        test.Expect(result.outcome == Outcome::Completed, "value zero outcome");
        test.Expect(fence.armCalls == 0, "value zero must not arm");
    }
    {
        StubFence fence;
        fence.before = 5;
        fence.after = 6;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::Completed, "normal wake outcome");
        test.Expect(fence.armCalls == 1, "normal wake arms once");
        test.Expect(fence.waitCalls == 1, "normal wake waits once");
        test.Expect(result.completedAfter == 6, "normal wake completed value");
    }
    {
        StubFence fence;
        fence.before = 5;
        fence.after = 9;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::Completed,
                    "completed beyond requested is success");
        test.Expect(result.completedAfter == 9, "beyond requested value recorded");
    }
}

void TestFailureModes(TestState& test)
{
    {
        // Регрессия: событие signaled, но fence всё ещё ниже запрошенного.
        StubFence fence;
        fence.before = 5;
        fence.after = 5;
        fence.waitStatus = WaitStatus::Signaled;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::SpuriousWake,
                    "stale wake must not be success");
        test.Expect(fence.armCalls == 1, "stale wake arms once");
        test.Expect(fence.waitCalls == 1, "stale wake waits once");
        test.Expect(result.completedAfter < 6, "stale wake records low value");
    }
    {
        StubFence fence;
        fence.before = 5;
        fence.waitStatus = WaitStatus::Timeout;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::Timeout, "timeout outcome");
    }
    {
        StubFence fence;
        fence.before = 5;
        fence.waitStatus = WaitStatus::Failed;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::WaitFailed, "wait failure outcome");
    }
    {
        StubFence fence;
        fence.before = 5;
        fence.armResult = false;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::ArmFailed, "arm failure outcome");
        test.Expect(fence.waitCalls == 0, "arm failure must not wait");
    }
}

void TestDeviceRemoved(TestState& test)
{
    {
        StubFence fence;
        fence.before = kRemovedMarker;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::DeviceRemoved,
                    "removed before wait");
        test.Expect(fence.armCalls == 0, "removed before wait must not arm");
        test.Expect(fence.waitCalls == 0, "removed before wait must not wait");
    }
    {
        StubFence fence;
        fence.before = 5;
        fence.after = kRemovedMarker;
        fence.waitStatus = WaitStatus::Signaled;
        const Result result = WaitForValue(fence, 6, 5000);
        test.Expect(result.outcome == Outcome::DeviceRemoved,
                    "removed after wait");
        test.Expect(fence.waitCalls == 1, "removed after wait observed wait");
    }
}

void TestLifetimeClassification(TestState& test)
{
    using enum GpuLifetime;

    test.Expect(ClassifyGpuLifetime(false, false, false, true) == NoWorkSubmitted,
                "no work has priority");
    test.Expect(ClassifyGpuLifetime(true, false, false, true) == Unknown,
                "missing synchronization is unknown");
    test.Expect(ClassifyGpuLifetime(true, true, true, false) == WorkCompleted,
                "confirmed drain is completed");
    test.Expect(ClassifyGpuLifetime(true, true, false, true) == DeviceRemoved,
                "confirmed removal after failed drain");
    test.Expect(ClassifyGpuLifetime(true, true, false, false) == Unknown,
                "failed drain without removal is unknown");

    // Исчерпывающая проверка главного инварианта: неуспешный drain не может
    // превратиться в WorkCompleted ни при какой комбинации остальных флагов.
    for (int work = 0; work <= 1; ++work)
    {
        for (int sync = 0; sync <= 1; ++sync)
        {
            for (int removed = 0; removed <= 1; ++removed)
            {
                const GpuLifetime lifetime = ClassifyGpuLifetime(
                    work != 0, sync != 0, false, removed != 0);
                test.Expect(lifetime != WorkCompleted,
                            "failed drain never becomes WorkCompleted");
            }
        }
    }
}
// L1: тот же helper, который вызывает CollectGpuTiming после fence/drain.
void TestTimestampMapFailure(TestState& test)
{
    struct Frame {
        bool timestampPending = true, timestampSample = true;
        int32_t timestampSampleIndex = 511;
    } frame;
    std::array<double, 512> samples;
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = 100.0 + double(i);
    const auto published = samples;
    unsigned reads = 0, aggregates = 0;
    bool failMap = true;
    bool reversed = false;
    const auto read = [&](uint64_t (&ticks)[2]) {
        ++reads;
        if (failMap) return false; // Управляемый отказ Map, не случайный OOM.
        ticks[0] = 100;
        ticks[1] = reversed ? 99 : 300;
        return true;
    };
    const auto publish = [&](int32_t index, double ms) {
        ++aggregates;
        if (index >= 0 && static_cast<size_t>(index) < samples.size())
            samples[index] = ms;
    };
    using dx12timing12::Consume;
    using dx12timing12::ConsumeResult;
    test.Expect(Consume(frame, 1000, read, publish) == ConsumeResult::MapFailed,
                "timestamp Map failure observed");
    test.Expect(!frame.timestampPending && !frame.timestampSample &&
                frame.timestampSampleIndex == -1, "failed Map clears binding");
    test.Expect(samples == published && aggregates == 0, "failed Map does not publish");

    // Все 512 samples уже заняты. Новый кадр не получает нового индекса.
    frame.timestampPending = frame.timestampSample = true;
    failMap = false;
    test.Expect(Consume(frame, 1000, read, publish) == ConsumeResult::Consumed,
                "later readback succeeds after Map failure");
    test.Expect(samples == published, "exhausted 512 samples remain unchanged");
    test.Expect(aggregates == 1, "unbound frame contributes only to aggregate");
    test.Expect(frame.timestampSampleIndex == -1, "unbound frame stays unbound");

    // Новая валидная привязка меняет только свой сэмпл.
    frame.timestampPending = frame.timestampSample = true;
    frame.timestampSampleIndex = 7;
    Consume(frame, 1000, read, publish);
    auto expected = published;
    expected[7] = 200.0;
    test.Expect(samples == expected, "valid GPU sample uses its own frame index");
    test.Expect(aggregates == 2, "valid GPU sample counted once");
    Consume(frame, 1000, read, publish);
    test.Expect(reads == 3 && aggregates == 2, "consumed timestamp cannot be read twice");

    frame.timestampPending = frame.timestampSample = true;
    frame.timestampSampleIndex = 511;
    test.Expect(Consume(frame, 0, read, publish) == ConsumeResult::Empty &&
                frame.timestampSampleIndex == -1 && reads == 3,
                "unavailable frequency invalidates binding without Map");
    frame.timestampPending = frame.timestampSample = true;
    frame.timestampSampleIndex = 7;
    reversed = true;
    Consume(frame, 1000, read, publish);
    test.Expect(samples == expected && aggregates == 2 && frame.timestampSampleIndex == -1,
                "invalid timestamp interval cannot publish or retain binding");
}
} // namespace

bool RunFenceWaitTests()
{
    TestState test;
    TestCompletedAndAlreadyComplete(test);
    TestFailureModes(test);
    TestDeviceRemoved(test);
    TestLifetimeClassification(test);
    TestTimestampMapFailure(test);

    std::printf("[fencewait] %s checks=%u failures=%u\n",
                test.failures == 0 ? "PASS" : "FAIL", test.checks, test.failures);
    return test.failures == 0;
}
} // namespace dx12fence12
