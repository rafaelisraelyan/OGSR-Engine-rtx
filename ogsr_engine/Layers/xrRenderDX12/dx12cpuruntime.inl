// Только standalone smoke: владелец JobSystem — локальный объект WinMain.
static gr::jobs::JobSystem* g_jobSystem = nullptr;
static bool g_jobSystemReady = false;
static bool g_jobsParallel = false;
static uint32_t g_jobWorkers = 0;
static uint32_t g_jobGrain = 64;
static uint32_t g_stressObjects = 0;
static bool g_cpuVerify = false;
static bool g_jobsAvailable = true;
static bool g_cpuSelfTest = false;
static bool g_resizeTest = false;
static float g_sceneAspect = kAspectW / kAspectH;
static uint32_t g_cpuTestFrames = 0;
static uint32_t g_cpuTestRemaining = 0;
static uint32_t g_cpuWarmupFrames = 0;
static uint64_t g_cpuStep = 0;
static uint32_t g_runtimeResizes = 0;
static uint32_t g_jobStarts = 0;

struct CpuSample { double serial, parallel, submit, wait, reduce, overhead; };
struct CpuSceneState
{
    std::vector<dx12cpuscene::ObjectDesc> objects;
    std::vector<dx12cpuscene::ObjectState> statesSerial, statesParallel;
    std::vector<CpuSample> samples;
    uint64_t frames = 0;
    uint64_t mismatchCount = 0;
    uint32_t verifyFrames = 0;
    float maxDeviation = 0;
    dx12cpuscene::PrepareResult lastSerial, lastParallel;
};
static CpuSceneState g_cpuScene;

static void StopJobs()
{
    if (!g_jobSystemReady) return;
    const auto stats = g_jobSystem->GetStats();
    g_jobSystem->Shutdown();
    g_jobSystemReady = false;
    dx12log::Write("[jobs] stopped: starts=%u submitted=%llu completed=%llu failures=%llu unobserved=%llu participants=%u",
        g_jobStarts, (unsigned long long)stats.tasksSubmitted, (unsigned long long)stats.tasksCompleted,
        (unsigned long long)stats.jobFailures, (unsigned long long)stats.unobservedFailures,
        g_jobSystem->GetTotalParticipantCount());
}

static void PrepareSceneCPU()
{
    auto& scene = g_cpuScene;
    if (scene.objects.empty()) return;
    // В автотесте время воспроизводимо; камера исходных двух кубов не меняется.
    const float time = g_cpuTestFrames ? static_cast<float>(g_cpuStep) / 60.0f : g_cameraTime;
    dx12cpuscene::CameraSnapshot camera{};
    dx12cpuscene::BuildCameraSnapshot(camera, time, g_sceneAspect);
    dx12cpuscene::PrepareResult serial, parallel;
    const auto runSerial = [&] {
        serial = dx12cpuscene::PrepareSerial(scene.statesSerial.data(), scene.objects.data(),
            g_stressObjects, camera, time);
    };
    const auto runParallel = [&] {
        parallel = dx12cpuscene::PrepareParallel(*g_jobSystem, scene.statesParallel.data(),
            scene.objects.data(), g_stressObjects, camera, time, g_jobGrain);
    };
    if (g_cpuVerify)
    {
        // Чередуем порядок, чтобы уменьшить преимущество второго прохода от кеша.
        if (g_cpuStep & 1) { runParallel(); runSerial(); }
        else { runSerial(); runParallel(); }
        float deviation = 0;
        scene.mismatchCount += dx12cpuscene::CompareStates(scene.statesSerial.data(),
            scene.statesParallel.data(), g_stressObjects, 1e-4f, &deviation);
        if (serial.visible != parallel.visible || serial.checksum != parallel.checksum)
            ++scene.mismatchCount;
        scene.maxDeviation = (std::max)(scene.maxDeviation, deviation);
        if (g_cpuWarmupFrames == 0) ++scene.verifyFrames;
    }
    else if (g_jobsParallel) runParallel();
    else runSerial();
    scene.lastSerial = serial;
    scene.lastParallel = parallel;
    if (g_cpuWarmupFrames == 0)
    {
        ++scene.frames;
        // Пустое задание измеряется отдельно и не вычитается из полной стоимости.
        const double overhead = (g_cpuTestFrames && (g_cpuVerify || g_jobsParallel)) ?
            dx12cpuscene::MeasureDispatchOverhead(*g_jobSystem, g_stressObjects, g_jobGrain) : 0;
        if (g_cpuTestFrames)
            scene.samples.push_back({serial.elapsedMs, parallel.elapsedMs, parallel.submitMs,
                parallel.waitMs, parallel.reduceMs, overhead});
    }
    ++g_cpuStep;
}

static void ReportCpuScene()
{
    const auto& scene = g_cpuScene;
    dx12log::Write("[cpu] objects=%u frames=%llu mode=%s workers=%u grain=%u verifyFrames=%u mismatches=%llu maxDev=%.6g",
        g_stressObjects, (unsigned long long)scene.frames, g_jobsParallel ? "parallel" : "serial",
        g_jobSystemReady ? g_jobSystem->GetBackgroundWorkerCount() : 0, g_jobGrain,
        scene.verifyFrames, (unsigned long long)scene.mismatchCount, scene.maxDeviation);
    dx12log::Write("[cpu] visible serial=%u parallel=%u checksum serial=%llu parallel=%llu",
        scene.lastSerial.visible, scene.lastParallel.visible,
        (unsigned long long)scene.lastSerial.checksum, (unsigned long long)scene.lastParallel.checksum);
    const auto metric = [&](const char* name, double CpuSample::* field) {
        if (scene.samples.empty()) return;
        std::vector<double> sorted;
        double sum = 0;
        for (const auto& sample : scene.samples) { sorted.push_back(sample.*field); sum += sample.*field; }
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double p) { return sorted[static_cast<size_t>(p * (sorted.size() - 1))]; };
        dx12log::Write("[cpu-metric] %s mean=%.6f p50=%.6f p95=%.6f p99=%.6f ms",
            name, sum / sorted.size(), percentile(.50), percentile(.95), percentile(.99));
    };
    if (g_cpuVerify || !g_jobsParallel) metric("serial-total", &CpuSample::serial);
    if (g_cpuVerify || g_jobsParallel)
    {
        metric("parallel-total", &CpuSample::parallel);
        metric("submit", &CpuSample::submit);
        metric("wait-helping", &CpuSample::wait);
        metric("reduce", &CpuSample::reduce);
        metric("empty-dispatch", &CpuSample::overhead);
    }
    for (size_t i = 0; i < scene.samples.size(); ++i)
    {
        const auto& v = scene.samples[i];
        dx12log::Write("[cpu-sample] %zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f",
            i, v.serial, v.parallel, v.submit, v.wait, v.reduce, v.overhead);
    }
    dx12log::Write("[cpu] CPU-only timings; wait includes caller work; empty-dispatch is a separate estimate, not FPS");
}
