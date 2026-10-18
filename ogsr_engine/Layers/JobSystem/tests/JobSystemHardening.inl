// Регрессии F3.2C.1. Подключается после исходных 25 тестов.
namespace {

GR_TEST(OwnershipAcrossContainerGrowth)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(4)));
    auto group = system.CreateGroup();
    std::atomic<unsigned> calls[512]{};
    std::vector<std::weak_ptr<unsigned>> lifetimes;
    for (unsigned i = 0; i < 512; ++i) {
        auto token = std::make_shared<unsigned>(i);
        lifetimes.push_back(token);
        group.Submit([token, &calls] { ++calls[*token]; });
    }
    bool owned = true;
    for (const auto& weak : lifetimes) owned &= !weak.expired();
    GR_CHECK(owned);
    auto moved = std::move(group);
    moved.Wait();
    bool once = true;
    bool released = true;
    for (unsigned i = 0; i < 512; ++i) {
        once &= calls[i].load() == 1;
        released &= lifetimes[i].expired();
    }
    GR_CHECK(once);
    GR_CHECK(released);
    GR_CHECK(!group.IsValid());
    GR_CHECK_EQ(system.GetStats().tasksSubmitted, 512u);
    GR_CHECK_EQ(system.GetStats().tasksCompleted, 512u);
}

std::atomic<unsigned> hardeningHookCalls{0};
void ThrowingDiagnosticHook(const char*, const char*)
{
    ++hardeningHookCalls;
    throw 17;
}

GR_TEST(ThrowingDiagnosticHookIsContained)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
    const auto before = gr::jobs::GetUnobservedJobErrorCount();
    hardeningHookCalls = 0;
    gr::jobs::SetUnobservedJobErrorHook(ThrowingDiagnosticHook);
    {
        auto group = system.CreateGroup();
        group.Submit([] { throw std::runtime_error("callback failure"); });
        group.DrainNoThrow();
        group.Submit([] { throw 42; });
        // Деструктор вызывает hook, выбрасывающий исключение не-std типа.
    }
    {
        auto victim = system.CreateGroup();
        victim.Submit([] { throw std::runtime_error("move failure"); });
        victim = system.CreateGroup();
    }
    gr::jobs::SetUnobservedJobErrorHook(nullptr);
    GR_CHECK_EQ(hardeningHookCalls.load(), 3u);
    GR_CHECK_EQ(gr::jobs::GetUnobservedJobErrorCount(), before + 3);
    unsigned done = 0;
    system.ParallelFor(1, [&](uint32_t, uint32_t, uint32_t) { ++done; });
    GR_CHECK_EQ(done, 1u);
}

gr::jobs::JobSystem* hookSystem = nullptr;
void RecursiveDiagnosticHook(const char*, const char*)
{
    ++hardeningHookCalls;
    auto nested = hookSystem->CreateGroup();
    nested.Submit([] { throw 7; });
}

GR_TEST(RecursiveDiagnosticHookIsBounded)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
    hookSystem = &system;
    hardeningHookCalls = 0;
    const auto before = gr::jobs::GetUnobservedJobErrorCount();
    gr::jobs::SetUnobservedJobErrorHook(RecursiveDiagnosticHook);
    {
        auto group = system.CreateGroup();
        group.Submit([] { throw 8; });
    }
    gr::jobs::SetUnobservedJobErrorHook(nullptr);
    hookSystem = nullptr;
    GR_CHECK_EQ(hardeningHookCalls.load(), 1u);
    GR_CHECK_EQ(gr::jobs::GetUnobservedJobErrorCount(), before + 2);
}

GR_TEST(RetiredGroupsCannotPublishToNewGeneration)
{
    gr::jobs::JobSystem system;
    for (unsigned iteration = 0; iteration < 4; ++iteration) {
        GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
        auto old = system.CreateGroup();
        old.Submit([] { throw std::runtime_error("old generation"); });
        system.Shutdown();
        GR_CHECK(system.Initialize(ConfigWithWorkers(4)));
        unsigned unexpected = 0;
        old.Submit([&] { ++unexpected; });
        old.ParallelFor(32, [&](uint32_t, uint32_t, uint32_t) { ++unexpected; });
        bool caught = false;
        try { old.Wait(); } catch (const std::runtime_error&) { caught = true; }
        GR_CHECK(caught);
        GR_CHECK(old.IsComplete());
        GR_CHECK_EQ(unexpected, 0u);
        GR_CHECK_EQ(system.GetStats().tasksSubmitted, 0u);
        GR_CHECK_EQ(system.GetStats().jobFailures, 0u);
        std::atomic<unsigned> done{0};
        system.ParallelFor(128, [&](uint32_t b, uint32_t e, uint32_t) { done += e - b; });
        GR_CHECK_EQ(done.load(), 128u);
        system.Shutdown();
        system.Shutdown();
    }
}

GR_TEST(ShutdownWaitsForActiveTaskOwnership)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
    auto group = system.CreateGroup();
    std::atomic<bool> started{false}, release{false}, done{false};
    auto token = std::make_shared<unsigned>(99);
    std::weak_ptr<unsigned> weak = token;
    group.Submit([token, &started, &release, &done] {
        started = true;
        while (!release.load()) std::this_thread::yield();
        done = *token == 99;
    });
    token.reset();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!started.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    GR_CHECK(started.load());
    GR_CHECK(!weak.expired());
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        release = true;
    });
    system.Shutdown();
    releaser.join();
    GR_CHECK(done.load());
    GR_CHECK(!weak.expired());
    group.Wait();
    GR_CHECK(weak.expired());
    GR_CHECK_EQ(system.GetStats().tasksCompleted, 1u);
}

GR_TEST(LifecycleCallsFromCallbackAreRejected)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
    auto group = system.CreateGroup();
    group.Submit([&] { system.Shutdown(); });
    bool caught = false;
    try { group.Wait(); } catch (const std::logic_error&) { caught = true; }
    GR_CHECK(caught);
    GR_CHECK(system.IsInitialized());
    bool initialized = true;
    group.Submit([&] { initialized = system.Initialize(ConfigWithWorkers(2)); });
    group.Wait();
    GR_CHECK(!initialized);
    system.Shutdown();
    GR_CHECK(!system.IsInitialized());
}

#ifdef GR_JOBS_TESTING
GR_TEST(RegistrationAllocationFailureBeforePublication)
{
    for (bool range : {false, true}) {
        gr::jobs::JobSystem system;
        GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
        auto group = system.CreateGroup();
        std::atomic<unsigned> executed{0};
        auto token = std::make_shared<int>(1);
        std::weak_ptr<int> weak = token;
        const auto before = gr::jobs::testing::registrationFailures;
        gr::jobs::testing::failRegistration = true;
        bool caught = false;
        try {
            if (range) group.ParallelFor(64, [token, &executed](uint32_t b, uint32_t e, uint32_t) { executed += e - b; });
            else group.Submit([token, &executed] { ++executed; });
        } catch (const std::bad_alloc&) { caught = true; }
        token.reset();
        GR_CHECK(caught);
        GR_CHECK_EQ(gr::jobs::testing::registrationFailures, before + 1);
        GR_CHECK(weak.expired());
        GR_CHECK_EQ(system.GetStats().tasksSubmitted, 0u);
        GR_CHECK(group.IsComplete());
        group.Wait();
        GR_CHECK_EQ(executed.load(), 0u);

        // Второй отказ — при росте уже непустого контейнера. Принятые ранее
        // задачи сохраняют владельцев; повторная отправка после отказа работает.
        group.Submit([&] { ++executed; });
        unsigned accepted = 1;
        gr::jobs::testing::failRegistration = true;
        caught = false;
        for (unsigned i = 0; i < 1024 && !caught; ++i) {
            try {
                if (range) group.ParallelFor(1, [&](uint32_t, uint32_t, uint32_t) { ++executed; });
                else group.Submit([&] { ++executed; });
                ++accepted;
            } catch (const std::bad_alloc&) { caught = true; }
        }
        gr::jobs::testing::failRegistration = false;
        GR_CHECK(caught);
        group.Submit([&] { ++executed; });
        group.Wait();
        GR_CHECK_EQ(executed.load(), accepted + 1);
        GR_CHECK_EQ(system.GetStats().tasksSubmitted, accepted + 1);
        GR_CHECK_EQ(system.GetStats().tasksCompleted, accepted + 1);
    }
}

GR_TEST(RejectedSubmissionRollsBackOwnership)
{
    gr::jobs::JobSystem system;
    GR_CHECK(system.Initialize(ConfigWithWorkers(2)));
    auto group = system.CreateGroup();
    std::atomic<unsigned> executed{0};
    group.Submit([&] { ++executed; });
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto token = std::make_shared<int>(1);
        std::weak_ptr<int> weak = token;
        gr::jobs::testing::failSubmit = true;
        bool caught = false;
        try {
            if (mode == 0) group.Submit([token, &executed] { ++executed; });
            if (mode == 1) group.ParallelFor(64, [token, &executed](uint32_t b, uint32_t e, uint32_t) { executed += e - b; });
            if (mode == 2) system.ParallelFor(64, [token, &executed](uint32_t b, uint32_t e, uint32_t) { executed += e - b; });
        } catch (const std::runtime_error&) { caught = true; }
        token.reset();
        GR_CHECK(caught);
        GR_CHECK(weak.expired());
        GR_CHECK_EQ(system.GetStats().tasksSubmitted, 1u);
    }
    group.Wait();
    GR_CHECK_EQ(executed.load(), 1u);
    group.Submit([&] { ++executed; });
    group.Wait();
    GR_CHECK_EQ(executed.load(), 2u);
    GR_CHECK_EQ(system.GetStats().tasksCompleted, 2u);
}

GR_TEST(BackendRejectsUninitializedNullAndRunningTask)
{
    gr::jobs::EnkiBackend backend;
    std::atomic<unsigned> calls{0};
    std::atomic<bool> release{false};
    auto errors = std::make_shared<gr::jobs::JobErrorSlot>();
    gr::jobs::InternalJobTask task(1, 1,
        [&](enki::TaskSetPartition, uint32_t) {
            ++calls;
            while (!release.load()) std::this_thread::yield();
        }, errors, gr::jobs::JobPriority::Normal);
    GR_CHECK(!backend.Submit(&task));
    GR_CHECK(backend.Initialize(2, true));
    GR_CHECK(!backend.Submit(nullptr));
    GR_CHECK(backend.Submit(&task));
    GR_CHECK(!backend.Submit(&task));
    release = true;
    backend.Wait(&task);
    GR_CHECK_EQ(calls.load(), 1u);
    backend.Shutdown();
    GR_CHECK(!backend.Submit(&task));
}
#endif

} // namespace
