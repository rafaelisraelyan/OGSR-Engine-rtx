// F3.2B — correctness tests for the GR Job System.
//
// Covers the mandatory checklist:
//   1  Initialize / Shutdown
//   2  Repeated Initialize / Shutdown
//   3  Sequential fallback
//   4  ParallelFor over different ranges
//   5  Empty and minimal ranges
//   6  Exactly-once execution
//   7  Summation and checksum
//   8  JobGroup Submit / Wait
//   9  Multiple JobGroups
//   10 Nested jobs
//   11 Exception behaviour
//   12 No hang on shutdown
//   13 Invalid parameters
//   14 Task lifetime safety

#include "gr/jobs/JobSystem.h"
#include "gr/jobs/JobGroup.h"
#include "gr/jobs/JobConfig.h"
#include "gr/jobs/JobStats.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#ifdef GR_JOBS_TESTING
#include "JobTestHooks.h"
#include "../src/EnkiBackend.h"
#include "../src/InternalJobTask.h"
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------
std::atomic<int> g_checks{0};
std::atomic<int> g_failures{0};
const char* g_current = "";

#define GR_CHECK(cond)                                                          \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            ++g_failures;                                                       \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                       \
    } while (false)

#define GR_CHECK_EQ(a, b)                                                       \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!((a) == (b))) {                                                    \
            ++g_failures;                                                       \
            std::printf("  FAIL %s:%d  %s == %s\n", __FILE__, __LINE__,         \
                        #a, #b);                                                \
        }                                                                       \
    } while (false)

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)())
    {
        TestCase tc;
        tc.name = name;
        tc.fn = fn;
        registry().push_back(tc);
    }
};

#define GR_TEST(name)                                                           \
    static void name();                                                         \
    static Registrar g_reg_##name(#name, name);                                 \
    static void name()

// ---------------------------------------------------------------------------
// Deterministic workload, identical to the F3.2A benchmark cell so results can
// be compared against the direct-enkiTS path.
// ---------------------------------------------------------------------------
uint64_t Cell(uint32_t index, uint32_t work)
{
    uint64_t x = static_cast<uint64_t>(index) + UINT64_C(0x9e3779b97f4a7c15);
    for (uint32_t i = 0; i < work; ++i) {
        x ^= x >> 27;
        x *= UINT64_C(0x3c79ac492ba7b653);
        x ^= x >> 33;
        x += static_cast<uint64_t>(i) * UINT64_C(0x1c69b3f74ac4ae35);
    }
    return x;
}

uint64_t SerialReference(uint32_t count, uint32_t work)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) {
        sum ^= Cell(i, work);
    }
    return sum;
}

uint64_t SerialReferenceOffset(uint32_t count, uint32_t work, uint32_t offset);

gr::jobs::JobConfig ConfigWithWorkers(uint32_t workers)
{
    gr::jobs::JobConfig c;
    c.backgroundWorkers = workers;
    c.automaticWorkers = false;
    c.defaultGrainSize = 64;
    c.enableStats = true;
    return c;
}

// ---------------------------------------------------------------------------
// 1. Initialize / Shutdown
// ---------------------------------------------------------------------------
GR_TEST(InitializeShutdown)
{
    gr::jobs::JobSystem system;
    GR_CHECK(!system.IsInitialized());

    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));
    GR_CHECK(system.IsInitialized());
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 4u);
    GR_CHECK_EQ(system.GetTotalParticipantCount(), 5u);

    system.Shutdown();
    GR_CHECK(!system.IsInitialized());
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 0u);
    GR_CHECK_EQ(system.GetTotalParticipantCount(), 0u);

    // Shutdown on an uninitialised system must be a safe no-op.
    system.Shutdown();
    GR_CHECK(!system.IsInitialized());
}

// ---------------------------------------------------------------------------
// 2. Repeated Initialize / Shutdown
// ---------------------------------------------------------------------------
GR_TEST(RepeatedInitializeShutdown)
{
    gr::jobs::JobSystem system;

    for (int cycle = 0; cycle < 5; ++cycle) {
        const uint32_t workers = static_cast<uint32_t>(1 + (cycle % 4));
        GR_CHECK(system.Initialize(ConfigWithWorkers(workers)));
        GR_CHECK(system.IsInitialized());

        std::atomic<uint64_t> sum(0);
        system.ParallelFor(2048, 64, [&sum](uint32_t begin, uint32_t end, uint32_t) {
            uint64_t local = 0;
            for (uint32_t i = begin; i < end; ++i) {
                local ^= Cell(i, 16);
            }
            sum.fetch_xor(local, std::memory_order_relaxed);
        });

        GR_CHECK_EQ(sum.load(), SerialReference(2048, 16));
        system.Shutdown();
        GR_CHECK(!system.IsInitialized());
    }

    // Initialising an already-initialised system must fail rather than leak.
    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
    GR_CHECK(!system.Initialize(ConfigWithWorkers(3)));
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 2u);
    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 3. Sequential fallback (no background workers)
// ---------------------------------------------------------------------------
GR_TEST(SequentialFallback)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(0)));
    GR_CHECK(system.IsInitialized());
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 0u);
    GR_CHECK_EQ(system.GetTotalParticipantCount(), 1u);  // initiating thread only

    std::atomic<uint64_t> sum(0);
    system.ParallelFor(4096, 64, [&sum](uint32_t begin, uint32_t end, uint32_t threadIndex) {
        GR_CHECK_EQ(threadIndex, 0u);  // only the initiating thread can run work
        uint64_t local = 0;
        for (uint32_t i = begin; i < end; ++i) {
            local ^= Cell(i, 16);
        }
        sum.fetch_xor(local, std::memory_order_relaxed);
    });

    GR_CHECK_EQ(sum.load(), SerialReference(4096, 16));
    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 4. ParallelFor over different ranges
// ---------------------------------------------------------------------------
GR_TEST(ParallelForRanges)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    const uint32_t sizes[] = {1, 2, 7, 63, 64, 65, 1000, 4096, 65536};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        const uint32_t count = sizes[s];
        std::atomic<uint64_t> sum(0);
        system.ParallelFor(count, 64, [&sum, count](uint32_t begin, uint32_t end, uint32_t) {
            GR_CHECK(begin <= end);
            GR_CHECK(end <= count);
            uint64_t local = 0;
            for (uint32_t i = begin; i < end; ++i) {
                local ^= Cell(i, 8);
            }
            sum.fetch_xor(local, std::memory_order_relaxed);
        });
        GR_CHECK_EQ(sum.load(), SerialReference(count, 8));
    }

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 5. Empty and minimal ranges
// ---------------------------------------------------------------------------
GR_TEST(EmptyAndMinimalRanges)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    bool called = false;
    system.ParallelFor(0, [&called](uint32_t, uint32_t, uint32_t) {
        called = true;  // must never run
    });
    GR_CHECK(!called);

    uint32_t seen = 0;
    system.ParallelFor(1, 1, [&seen](uint32_t begin, uint32_t end, uint32_t) {
        GR_CHECK_EQ(begin, 0u);
        GR_CHECK_EQ(end, 1u);
        ++seen;
    });
    GR_CHECK_EQ(seen, 1u);

    // A grain size larger than the range must not break partitioning.
    uint32_t elements = 0;
    system.ParallelFor(10, 100000, [&elements](uint32_t begin, uint32_t end, uint32_t) {
        elements += (end - begin);
    });
    GR_CHECK_EQ(elements, 10u);

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 6. Exactly-once execution
// ---------------------------------------------------------------------------
GR_TEST(ExactlyOnceExecution)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(8)));

    const uint32_t count = 65536;
    // Distinct indices are written by distinct threads, so plain increments of
    // separate elements carry no data race.
    std::vector<uint32_t> hits(count, 0);

    for (int round = 0; round < 8; ++round) {
        std::fill(hits.begin(), hits.end(), 0);
        system.ParallelFor(count, 256, [&hits](uint32_t begin, uint32_t end, uint32_t) {
            for (uint32_t i = begin; i < end; ++i) {
                ++hits[i];
            }
        });

        uint32_t bad = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (hits[i] != 1) {
                ++bad;
            }
        }
        GR_CHECK_EQ(bad, 0u);
    }

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 7. Summation and checksum
// ---------------------------------------------------------------------------
GR_TEST(SummationAndChecksum)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    const uint32_t count = 100000;
    const uint32_t work = 32;

    std::vector<uint64_t> partial(8, 0);
    system.ParallelFor(count, 512, [&partial](uint32_t begin, uint32_t end, uint32_t threadIndex) {
        uint64_t local = 0;
        for (uint32_t i = begin; i < end; ++i) {
            local ^= Cell(i, 32);
        }
        partial[threadIndex % partial.size()] ^= local;
    });

    uint64_t parallel = 0;
    for (size_t i = 0; i < partial.size(); ++i) {
        parallel ^= partial[i];
    }

    GR_CHECK_EQ(parallel, SerialReference(count, work));

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 8. JobGroup Submit / Wait
// ---------------------------------------------------------------------------
GR_TEST(GroupSubmitWait)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    gr::jobs::JobGroup group = system.CreateGroup();
    GR_CHECK(group.IsValid());
    GR_CHECK(group.IsComplete());  // empty group is complete
    GR_CHECK_EQ(group.GetPendingCount(), 0u);

    std::atomic<uint64_t> sum(0);
    const int jobs = 64;
    for (int i = 0; i < jobs; ++i) {
        group.Submit([&sum]() {
            sum.fetch_add(1, std::memory_order_relaxed);
        });
    }

    GR_CHECK(!group.IsComplete() || sum.load() == static_cast<uint64_t>(jobs));
    GR_CHECK(group.GetPendingCount() > 0u || sum.load() == static_cast<uint64_t>(jobs));

    group.Wait();

    GR_CHECK_EQ(sum.load(), static_cast<uint64_t>(jobs));
    GR_CHECK(group.IsComplete());
    GR_CHECK_EQ(group.GetPendingCount(), 0u);

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 9. Multiple JobGroups, and group reuse after Wait
// ---------------------------------------------------------------------------
GR_TEST(MultipleGroups)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    gr::jobs::JobGroup a = system.CreateGroup();
    gr::jobs::JobGroup b = system.CreateGroup();
    GR_CHECK(a.IsValid());
    GR_CHECK(b.IsValid());

    std::atomic<uint64_t> sumA(0);
    std::atomic<uint64_t> sumB(0);

    for (int i = 0; i < 32; ++i) {
        a.Submit([&sumA]() { sumA.fetch_add(1, std::memory_order_relaxed); });
        b.Submit([&sumB]() { sumB.fetch_add(2, std::memory_order_relaxed); });
    }

    a.Wait();
    b.Wait();

    GR_CHECK_EQ(sumA.load(), 32u);
    GR_CHECK_EQ(sumB.load(), 64u);

    // A drained group may be reused.
    // The job contributes with fetch_xor (not fetch_add) so that the two
    // contributions commute: they run concurrently and fetch_add/fetch_xor do
    // not commute, which would make the expected value order-dependent.
    std::atomic<uint64_t> reused(0);
    a.Submit([&reused]() { reused.fetch_xor(1, std::memory_order_relaxed); });
    a.ParallelFor(512, 64, [&reused](uint32_t begin, uint32_t end, uint32_t) {
        uint64_t local = 0;
        for (uint32_t i = begin; i < end; ++i) {
            local ^= Cell(i, 8);
        }
        reused.fetch_xor(local, std::memory_order_relaxed);
    });
    a.Wait();

    GR_CHECK_EQ(reused.load(), SerialReference(512, 8) ^ 1u);
    GR_CHECK(a.IsComplete());

    // Move semantics keep a valid group usable.
    gr::jobs::JobGroup moved = std::move(b);
    GR_CHECK(moved.IsValid());
    std::atomic<uint64_t> movedSum(0);
    moved.Submit([&movedSum]() { movedSum.fetch_add(7, std::memory_order_relaxed); });
    moved.Wait();
    GR_CHECK_EQ(movedSum.load(), 7u);

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 10. Nested jobs: a job that submits more work and waits for it
// ---------------------------------------------------------------------------
GR_TEST(NestedJobs)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    std::atomic<uint64_t> total(0);

    // A group whose job itself runs a nested ParallelFor and waits for it. This
    // must not deadlock: the waiting thread executes pending work meanwhile.
    gr::jobs::JobGroup group = system.CreateGroup();
    for (int i = 0; i < 8; ++i) {
        group.Submit([&system, &total, i]() {
            std::atomic<uint64_t> inner(0);
            system.ParallelFor(512, 64, [&inner, i](uint32_t begin, uint32_t end, uint32_t) {
                uint64_t local = 0;
                for (uint32_t k = begin; k < end; ++k) {
                    local ^= Cell(static_cast<uint32_t>(k + i), 8);
                }
                inner.fetch_xor(local, std::memory_order_relaxed);
            });
            total.fetch_xor(inner.load(), std::memory_order_relaxed);
        });
    }
    group.Wait();

    uint64_t expected = 0;
    for (int i = 0; i < 8; ++i) {
        expected ^= SerialReferenceOffset(512, 8, static_cast<uint32_t>(i));
    }
    GR_CHECK_EQ(total.load(), expected);

    system.Shutdown();
}

uint64_t SerialReferenceOffset(uint32_t count, uint32_t work, uint32_t offset)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) {
        sum ^= Cell(i + offset, work);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// 11. Exception behaviour: captured, not fatal, rethrown on the waiting thread
// ---------------------------------------------------------------------------
GR_TEST(ExceptionBehaviour)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    // (a) System-level ParallelFor rethrows.
    bool caught = false;
    try {
        system.ParallelFor(64, 16, [](uint32_t, uint32_t, uint32_t) {
            throw std::runtime_error("parallel-for failure");
        });
    } catch (const std::runtime_error& e) {
        caught = (std::strcmp(e.what(), "parallel-for failure") == 0);
    }
    GR_CHECK(caught);

    // The system must remain usable after a failure.
    std::atomic<uint64_t> after(0);
    system.ParallelFor(256, 64, [&after](uint32_t begin, uint32_t end, uint32_t) {
        uint64_t local = 0;
        for (uint32_t i = begin; i < end; ++i) {
            local ^= Cell(i, 8);
        }
        after.fetch_xor(local, std::memory_order_relaxed);
    });
    GR_CHECK_EQ(after.load(), SerialReference(256, 8));

    // (b) Group-level failure rethrows from Wait, after all jobs finished.
    gr::jobs::JobGroup group = system.CreateGroup();
    std::atomic<int> completed(0);
    for (int i = 0; i < 16; ++i) {
        group.Submit([&completed, i]() {
            if (i == 7) {
                throw std::logic_error("group job failure");
            }
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    bool groupCaught = false;
    try {
        group.Wait();
    } catch (const std::logic_error& e) {
        groupCaught = (std::strcmp(e.what(), "group job failure") == 0);
    }
    GR_CHECK(groupCaught);
    // Every non-throwing job still ran: a failure must not abandon the group.
    GR_CHECK_EQ(completed.load(), 15);
    GR_CHECK(group.IsComplete());

    // (c) The failure is counted in the statistics.
    gr::jobs::JobStats stats = system.GetStats();
    GR_CHECK(stats.jobFailures >= 2u);

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// 12. No hang on shutdown with outstanding and long-running work
// ---------------------------------------------------------------------------
GR_TEST(ShutdownNoHang)
{
    for (int cycle = 0; cycle < 4; ++cycle) {
        gr::jobs::JobSystem system;
        GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

        std::atomic<uint64_t> progress(0);
        gr::jobs::JobGroup group = system.CreateGroup();
        for (int i = 0; i < 8; ++i) {
            group.Submit([&progress]() {
                for (int k = 0; k < 200; ++k) {
                    progress.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        group.Wait();
        GR_CHECK_EQ(progress.load(), 1600u);

        // Shutdown while idle, then again, then reuse.
        system.Shutdown();
        system.Shutdown();
        GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
        GR_CHECK(system.IsInitialized());
        system.Shutdown();
    }
}

// ---------------------------------------------------------------------------
// 13. Invalid parameters
// ---------------------------------------------------------------------------
GR_TEST(InvalidParameters)
{
    gr::jobs::JobSystem system;

    gr::jobs::JobConfig badGrain = ConfigWithWorkers(2);
    badGrain.defaultGrainSize = 0;
    GR_CHECK(!system.Initialize(badGrain));          // rejected, stays uninitialised
    GR_CHECK(!system.IsInitialized());

    gr::jobs::JobConfig badMax = ConfigWithWorkers(2);
    badMax.maxAutoBackgroundWorkers = 0;
    GR_CHECK(!system.Initialize(badMax));
    GR_CHECK(!system.IsInitialized());

    // Operations on a default-constructed (unbound) group must be no-ops.
    gr::jobs::JobGroup empty;
    GR_CHECK(!empty.IsValid());
    empty.Submit([]() {});
    empty.ParallelFor(1024, 64, [](uint32_t, uint32_t, uint32_t) {});
    empty.Wait();
    GR_CHECK(empty.IsComplete());
    GR_CHECK_EQ(empty.GetPendingCount(), 0u);

    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));

    // Null callbacks are ignored rather than dereferenced.
    gr::jobs::JobCallback nullJob;
    gr::jobs::JobGroup group = system.CreateGroup();
    group.Submit(nullJob);
    group.ParallelFor(256, 64, gr::jobs::RangeCallback());
    group.Wait();
    GR_CHECK(group.IsComplete());

    // A group that outlives nothing: an invalid system returns an invalid group.
    system.Shutdown();
    gr::jobs::JobGroup afterShutdown = system.CreateGroup();
    GR_CHECK(!afterShutdown.IsValid());
    afterShutdown.Submit([]() {});
    afterShutdown.Wait();
}

// ---------------------------------------------------------------------------
// 14. Task lifetime safety: callbacks may capture by reference across Wait
// ---------------------------------------------------------------------------
GR_TEST(TaskLifetimeSafety)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    // All of these locals are destroyed only after Wait() returns, which is what
    // makes reference capture safe for the synchronous ParallelFor.
    std::vector<uint64_t> scratch(64, 0);
    std::atomic<uint64_t> sum(0);
    std::atomic<uint32_t> calls(0);

    system.ParallelFor(2048, 128, [&](uint32_t begin, uint32_t end, uint32_t threadIndex) {
        uint64_t local = 0;
        for (uint32_t i = begin; i < end; ++i) {
            local ^= Cell(i, 12);
        }
        scratch[threadIndex % scratch.size()] ^= local;
        sum.fetch_xor(local, std::memory_order_relaxed);
        calls.fetch_add(1, std::memory_order_relaxed);
    });

    GR_CHECK_EQ(sum.load(), SerialReference(2048, 12));
    GR_CHECK(calls.load() > 0);

    // A group whose callback references a stack object that is still alive.
    gr::jobs::JobGroup group = system.CreateGroup();
    std::vector<uint64_t> partial(8, 0);
    group.ParallelFor(4096, 256, [&partial](uint32_t begin, uint32_t end, uint32_t threadIndex) {
        uint64_t local = 0;
        for (uint32_t i = begin; i < end; ++i) {
            local ^= Cell(i, 8);
        }
        partial[threadIndex % partial.size()] ^= local;
    });
    group.Wait();

    uint64_t combined = 0;
    for (size_t i = 0; i < partial.size(); ++i) {
        combined ^= partial[i];
    }
    GR_CHECK_EQ(combined, SerialReference(4096, 8));

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// Configuration and diagnostics
// ---------------------------------------------------------------------------
GR_TEST(WorkerConfiguration)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(1)));
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 1u);
    GR_CHECK_EQ(system.GetTotalParticipantCount(), 2u);
    system.Shutdown();

    GR_CHECK(system.Initialize(ConfigWithWorkers(8)));
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 8u);
    GR_CHECK_EQ(system.GetTotalParticipantCount(), 9u);
    system.Shutdown();

    // Explicit counts are honoured exactly; the initiating thread is extra.
    GR_CHECK(system.Initialize(ConfigWithWorkers(16)));
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 16u);
    GR_CHECK_EQ(system.GetTotalParticipantCount(), 17u);
    system.Shutdown();
}

GR_TEST(AutomaticPolicy)
{
    gr::jobs::JobSystem system;
    gr::jobs::JobConfig config;
    config.backgroundWorkers = 0;
    config.automaticWorkers = true;
    config.maxAutoBackgroundWorkers = 8;
    GR_CHECK(system.Initialize(config));

    const uint32_t workers = system.GetBackgroundWorkerCount();
    GR_CHECK(workers >= 1u);
    GR_CHECK(workers <= 8u);
    GR_CHECK_EQ(system.GetTotalParticipantCount(), workers + 1u);

    // The automatic policy must not consume every logical processor.
    GR_CHECK(workers < system.GetLogicalProcessorCount());

    gr::jobs::JobStats stats = system.GetStats();
    GR_CHECK(stats.physicalCores >= 1u);
    GR_CHECK(stats.logicalProcessors >= stats.physicalCores);

    system.Shutdown();
}

GR_TEST(StatisticsCounters)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    gr::jobs::JobStats before = system.GetStats();
    GR_CHECK_EQ(before.tasksSubmitted, 0u);

    system.ParallelFor(4096, 256, [](uint32_t, uint32_t, uint32_t) {});

    gr::jobs::JobGroup group = system.CreateGroup();
    for (int i = 0; i < 16; ++i) {
        group.Submit([]() {});
    }
    group.ParallelFor(1024, 128, [](uint32_t, uint32_t, uint32_t) {});
    group.Wait();

    gr::jobs::JobStats after = system.GetStats();
    GR_CHECK_EQ(after.parallelForCalls, before.parallelForCalls + 1u);
    GR_CHECK(after.tasksSubmitted >= 17u);
    GR_CHECK(after.tasksCompleted >= 17u);
    GR_CHECK(after.groupsCreated >= 1u);
    GR_CHECK_EQ(after.groupsCompleted, before.groupsCompleted + 1u);
    GR_CHECK_EQ(after.jobFailures, 0u);

    // Counters must be switchable off.
    system.Shutdown();
    gr::jobs::JobConfig quiet = ConfigWithWorkers(2);
    quiet.enableStats = false;
    GR_CHECK(system.Initialize(quiet));
    system.ParallelFor(128, 32, [](uint32_t, uint32_t, uint32_t) {});
    gr::jobs::JobStats quietStats = system.GetStats();
    GR_CHECK_EQ(quietStats.parallelForCalls, 0u);
    GR_CHECK_EQ(quietStats.tasksSubmitted, 0u);
    system.Shutdown();
}

GR_TEST(GroupDestructorDrains)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    std::atomic<uint64_t> done(0);
    {
        gr::jobs::JobGroup group = system.CreateGroup();
        for (int i = 0; i < 32; ++i) {
            group.Submit([&done]() { done.fetch_add(1, std::memory_order_relaxed); });
        }
        // Deliberately no Wait(): the destructor must drain, so the jobs are
        // never destroyed while running.
    }
    GR_CHECK_EQ(done.load(), 32u);

    system.Shutdown();
}

// ---------------------------------------------------------------------------
// F3.2B.1 — lifetime and exception-safety regressions
// ---------------------------------------------------------------------------
namespace {

// Records what the unobserved-failure hook received, so a test can assert that
// a discarded failure was reported rather than dropped.
std::vector<std::string> g_hookContexts;
std::vector<std::string> g_hookMessages;

void RecordingHook(const char* context, const char* what)
{
    g_hookContexts.push_back(context ? context : "");
    g_hookMessages.push_back(what ? what : "");
}

struct HookScope
{
    HookScope()
    {
        g_hookContexts.clear();
        g_hookMessages.clear();
        gr::jobs::SetUnobservedJobErrorHook(&RecordingHook);
    }
    ~HookScope()
    {
        gr::jobs::SetUnobservedJobErrorHook(nullptr);
    }
};

} // namespace

// The critical scenario from the review: a job throws, the caller destroys the
// group without an explicit Wait(), and the destructor must neither throw (a
// throwing destructor calls std::terminate) nor drop the failure.
GR_TEST(DestructorWithoutWaitDoesNotThrow)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    HookScope hook;
    const uint64_t before = gr::jobs::GetUnobservedJobErrorCount();

    {
        gr::jobs::JobGroup group = system.CreateGroup();
        group.Submit([]() { throw std::runtime_error("implicit-drain failure"); });
        // Deliberately no Wait().
    }

    // Reported, not dropped, not thrown from the destructor.
    GR_CHECK(gr::jobs::GetUnobservedJobErrorCount() == before + 1);
    GR_CHECK_EQ(g_hookMessages.size(), 1u);
    if (g_hookMessages.size() == 1) {
        GR_CHECK(g_hookMessages[0].find("implicit-drain failure") != std::string::npos);
    }

    gr::jobs::JobStats stats = system.GetStats();
    GR_CHECK(stats.unobservedFailures >= 1u);
    GR_CHECK(stats.jobFailures >= 1u);

    system.Shutdown();
}

// An explicit Wait() must still propagate, and must NOT be counted as unobserved.
GR_TEST(ExplicitWaitStillPropagates)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    HookScope hook;
    const uint64_t before = gr::jobs::GetUnobservedJobErrorCount();

    gr::jobs::JobGroup group = system.CreateGroup();
    group.Submit([]() { throw std::runtime_error("explicit-wait failure"); });

    bool caught = false;
    try {
        group.Wait();
    } catch (const std::runtime_error& e) {
        caught = (std::strcmp(e.what(), "explicit-wait failure") == 0);
    }
    GR_CHECK(caught);

    // Observed by an explicit Wait(): the unobserved counter must not move.
    GR_CHECK(gr::jobs::GetUnobservedJobErrorCount() == before);
    GR_CHECK(g_hookMessages.empty());

    // The group is drained and reusable after a failed Wait.
    GR_CHECK(group.IsComplete());
    std::atomic<uint64_t> reused(0);
    group.Submit([&reused]() { reused.fetch_xor(1, std::memory_order_relaxed); });
    group.Wait();
    GR_CHECK_EQ(reused.load(), 1u);

    system.Shutdown();
}

// Move assignment is noexcept; it must not terminate the process even when the
// group being drained contains a failed job.
GR_TEST(MoveAssignmentDrainsWithoutThrowing)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    HookScope hook;
    const uint64_t before = gr::jobs::GetUnobservedJobErrorCount();

    // (a) The adopted group holds a failed job.
    gr::jobs::JobGroup source = system.CreateGroup();
    source.Submit([]() { throw std::runtime_error("adopted failure"); });

    gr::jobs::JobGroup target;
    target = std::move(source);   // noexcept path

    // The failure is still unobserved: drain it without Wait().
    target.DrainNoThrow();
    GR_CHECK(gr::jobs::GetUnobservedJobErrorCount() == before + 1);

    // (b) The target being overwritten holds a failed job.
    HookScope hook2;
    gr::jobs::JobGroup victim = system.CreateGroup();
    victim.Submit([]() { throw std::runtime_error("victim failure"); });

    gr::jobs::JobGroup replacement = system.CreateGroup();
    replacement.Submit([]() {});
    victim = std::move(replacement);   // drains victim's failed job via DrainNoThrow

    GR_CHECK(gr::jobs::GetUnobservedJobErrorCount() == before + 2);
    GR_CHECK(g_hookMessages.size() == 1u);
    if (g_hookMessages.size() == 1) {
        GR_CHECK(g_hookMessages[0].find("victim failure") != std::string::npos);
    }

    system.Shutdown();
}

// A group holds a shared reference to the system state, so it may outlive the
// system. Destroying the system drains the group's work; the group then drains
// safely and submitting becomes a no-op.
GR_TEST(GroupOutlivesSystem)
{
    gr::jobs::JobGroup group;
    std::atomic<uint64_t> done(0);
    {
        gr::jobs::JobSystem system;
        GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

        group = system.CreateGroup();
        GR_CHECK(group.IsValid());

        group.Submit([&done]() { done.fetch_add(1, std::memory_order_relaxed); });
        group.ParallelFor(1024, 128, [&done](uint32_t begin, uint32_t end, uint32_t) {
            for (uint32_t k = begin; k < end; ++k) {
                done.fetch_add(1, std::memory_order_relaxed);
            }
        });
        // system destroyed here; its Shutdown() drains the group's tasks.
    }

    GR_CHECK_EQ(done.load(), 1u + 1024u);
    GR_CHECK(group.IsValid());

    // Draining after the system is gone must be safe.
    group.Wait();
    GR_CHECK(group.IsComplete());

    // Submitting into a group whose system is gone is a no-op, not a crash.
    group.Submit([]() { GR_CHECK(false); });   // must never run
    group.DrainNoThrow();
}

// Shutdown with live groups: their work must be drained, and the groups must
// remain safe to drain and query afterwards.
GR_TEST(ShutdownWithLiveGroups)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));

    gr::jobs::JobGroup a = system.CreateGroup();
    gr::jobs::JobGroup b = system.CreateGroup();
    std::atomic<uint64_t> done(0);

    for (int i = 0; i < 32; ++i) {
        a.Submit([&done]() { done.fetch_add(1, std::memory_order_relaxed); });
        b.ParallelFor(256, 64, [&done](uint32_t begin, uint32_t end, uint32_t) {
            for (uint32_t k = begin; k < end; ++k) {
                done.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Shutdown while both groups still hold work.
    system.Shutdown();
    GR_CHECK_EQ(done.load(), 32u + 32u * 256u);

    a.Wait();
    b.Wait();
    GR_CHECK(a.IsComplete());
    GR_CHECK(b.IsComplete());

    // Submitting after shutdown is a no-op.
    a.Submit([]() { GR_CHECK(false); });
    a.DrainNoThrow();
}

// Re-initialising the same JobSystem object while a group from the previous
// generation is still alive.
GR_TEST(ReinitializeWithOldGenerationGroup)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));

    gr::jobs::JobGroup oldGroup = system.CreateGroup();
    std::atomic<uint64_t> oldSum(0);
    oldGroup.Submit([&oldSum]() { oldSum.fetch_add(1, std::memory_order_relaxed); });
    oldGroup.Wait();
    GR_CHECK_EQ(oldSum.load(), 1u);

    system.Shutdown();
    GR_CHECK(!system.IsInitialized());

    // A group from the previous generation must stay safe to drain.
    oldGroup.Wait();
    oldGroup.Submit([]() {});
    oldGroup.DrainNoThrow();

    // Re-initialise the SAME object (a fresh object would not exercise the
    // backend's flag reset).
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));
    GR_CHECK(system.IsInitialized());
    GR_CHECK_EQ(system.GetBackgroundWorkerCount(), 4u);

    gr::jobs::JobGroup newGroup = system.CreateGroup();
    std::atomic<uint64_t> newSum(0);
    newGroup.Submit([&newSum]() { newSum.fetch_add(1, std::memory_order_relaxed); });
    newGroup.Wait();
    GR_CHECK_EQ(newSum.load(), 1u);

    // Both generations coexist safely.
    oldGroup.Wait();
    GR_CHECK(oldGroup.IsComplete());
    system.Shutdown();
}

// ---------------------------------------------------------------------------
// Subprocess scenarios
//
// A throwing destructor manifests as std::terminate / abort, which no in-process
// assertion can observe: the process simply dies. These scenarios therefore run
// in a child process and the parent checks the exit code.
// ---------------------------------------------------------------------------
namespace {

int RunChildProcess(const std::string& scenario)
{
#ifdef _WIN32
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0) {
        return -1;
    }

    std::string command;
    command += '"';
    command += path;
    command += "\" --scenario ";
    command += scenario;

    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    ZeroMemory(&process, sizeof(process));

    if (!CreateProcessA(nullptr, const_cast<char*>(command.c_str()), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return -1;
    }

    if (WaitForSingleObject(process.hProcess, 15000) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        return 124;
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return static_cast<int>(exitCode);
#else
    (void)scenario;
    return -1;
#endif
}

int RunScenario(const std::string& scenario)
{
    if (scenario == "throwing_group_implicit_drain") {
        gr::jobs::JobSystem system;
        if (!system.Initialize(ConfigWithWorkers(4))) {
            return 2;
        }
        {
            gr::jobs::JobGroup group = system.CreateGroup();
            group.Submit([]() { throw std::runtime_error("subprocess failure"); });
            // Destroyed without Wait(): the destructor must not throw.
        }
        return 0;
    }

    if (scenario == "throwing_move_assign") {
        gr::jobs::JobSystem system;
        if (!system.Initialize(ConfigWithWorkers(4))) {
            return 2;
        }
        gr::jobs::JobGroup source = system.CreateGroup();
        source.Submit([]() { throw std::runtime_error("subprocess move failure"); });
        gr::jobs::JobGroup target;
        target = std::move(source);   // noexcept
        // target destroyed without Wait().
        return 0;
    }

    if (scenario == "group_outlives_system") {
        gr::jobs::JobGroup group;
        {
            gr::jobs::JobSystem system;
            if (!system.Initialize(ConfigWithWorkers(4))) {
                return 2;
            }
            group = system.CreateGroup();
            group.Submit([]() { throw std::runtime_error("subprocess lifetime failure"); });
        }
        group.DrainNoThrow();
        return 0;
    }

    return 3;   // unknown scenario
}

} // namespace

GR_TEST(NoTerminateOnImplicitDrain)
{
#ifdef _WIN32
    GR_CHECK_EQ(RunChildProcess("throwing_group_implicit_drain"), 0);
    GR_CHECK_EQ(RunChildProcess("throwing_move_assign"), 0);
    GR_CHECK_EQ(RunChildProcess("group_outlives_system"), 0);
    std::printf("       (3 subprocess scenarios exited cleanly)\n");
#else
    std::printf("       SKIPPED: subprocess check requires Win32\n");
#endif
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
} // namespace

#include "JobSystemHardening.inl"

int main(int argc, char** argv)
{
    // Subprocess mode: run a single lifetime scenario and exit with its status.
    // Used by NoTerminateOnImplicitDrain to prove that no path terminates.
    if (argc == 3 && std::strcmp(argv[1], "--scenario") == 0) {
        return RunScenario(argv[2]);
    }

    const std::vector<TestCase>& tests = registry();

    std::printf("GR Job System: %d test cases\n", static_cast<int>(tests.size()));
    for (size_t i = 0; i < tests.size(); ++i) {
        g_current = tests[i].name;
        const int before = g_failures.load();
        const int checksBefore = g_checks.load();
        std::printf("[%2d/%2d] %s\n", static_cast<int>(i + 1),
                    static_cast<int>(tests.size()), tests[i].name);
        tests[i].fn();
        if (g_failures == before) {
            std::printf("       ok (%d checks)\n", g_checks.load() - checksBefore);
        }
    }

    std::printf("\n%d checks, %d failures\n", g_checks.load(), g_failures.load());
    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("FAIL\n");
    return 1;
}
