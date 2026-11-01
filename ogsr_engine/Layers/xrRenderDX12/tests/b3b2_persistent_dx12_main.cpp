// F4.0-B3-B2B: persistent standalone DX12 consumer for B3-B2A live cameras.
//
// The verified B3-A standalone GPU helpers (device/fence/PSO/geometry/readback
// and audit) are reused directly from b3a_offscreen_main.cpp. This file adds
// the persistent single-slot frame loop, B3-B2A stream consumption, deterministic
// offline source and per-frame telemetry. No XR_3DA, Device, CLevel, DX11/R4 or
// game swapchain is linked.

#include "b3b2_persistent_dx12_helpers.h"

#include "b3b2_camera_stream.h"
#include "b3b2_persistent_policy.h"
#include "b3b2_persistent_evidence_validation.h"

#include <set>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <random>

using b3b2::CameraPacket;
using b3b2::CameraStreamClient;
using b3b2::CameraStreamProducer;
using b3b2::ReceiverValidationState;
using cameracapture12::CameraCaptureFile12;

namespace
{
constexpr uint32_t kPersistentFenceTimeoutMs = 10000;
constexpr uint32_t kPersistentVisual = 6470;
constexpr uint32_t kPersistentDefaultFrames = 30;
constexpr uint32_t kPersistentDefaultDurationMs = 30000;
constexpr uint32_t kPersistentDefaultSampleEvery = 10;
constexpr int kEvidenceUnverifiedExitCode = 3;

// Evidence paths are UTF-8 on disk; the Win32 filesystem is UTF-16. Convert
// explicitly instead of relying on the process ANSI code page.
std::string Utf8FromWide(const std::wstring& text)
{
    if (text.empty()) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                              static_cast<int>(text.size()), out.data(), size,
                              nullptr, nullptr) != size)
        return {};
    return out;
}

std::wstring WideFromUtf8(const std::string& text)
{
    if (text.empty()) return {};
    const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                              static_cast<int>(text.size()), out.data(), size) != size)
        return {};
    return out;
}

std::string PathToUtf8(const fs::path& path)
{
    return Utf8FromWide(path.native());
}

fs::path PathFromUtf8(const std::string& text)
{
    const std::wstring wide = WideFromUtf8(text);
    return wide.empty() ? fs::path() : fs::path(wide);
}

#ifdef B3B2B_ENABLE_FAULT_INJECTION
bool FaultRequested(const char* name) noexcept
{
    static const char* fault = std::getenv("B3B2B_FAULT");
    return fault != nullptr && std::strcmp(fault, name) == 0;
}

bool ReadFaultRequested(const fs::path& path, const char* kind) noexcept
{
    const char* target = std::getenv("B3B2B_READ_FAULT_TARGET");
    const char* fault = std::getenv("B3B2B_READ_FAULT");
    return target != nullptr && fault != nullptr &&
           std::strcmp(fault, kind) == 0 &&
           path.filename() == fs::path(target);
}
#else
bool FaultRequested(const char*) noexcept { return false; }
bool ReadFaultRequested(const fs::path&, const char*) noexcept { return false; }
#endif

struct PersistentOptions
{
    fs::path level;
    fs::path geom;
    fs::path output;
    std::vector<fs::path> offlineFixtures;
    std::wstring pipeName;
    std::string levelName = "jupiter";
    uint32_t visual = kPersistentVisual;
    uint32_t maxFrames = kPersistentDefaultFrames;
    uint32_t durationMs = kPersistentDefaultDurationMs;
    uint32_t sampleEvery = kPersistentDefaultSampleEvery;
    bool offline = false;
};

struct RunIdentity
{
    std::string id;
    std::string startedUtc;
    fs::path base;
    fs::path directory;
    fs::path manifest;
};

struct PersistentCounters
{
    uint64_t deviceCreateCount = 0;
    uint64_t queueCreateCount = 0;
    uint64_t psoCreateCount = 0;
    uint64_t geometryUploadCount = 0;
    uint64_t vertexBufferCreateCount = 0;
    uint64_t indexBufferCreateCount = 0;
    uint64_t renderTargetRecreateCount = 0;
    uint64_t framesReceived = 0;
    uint64_t framesSubmitted = 0;
    uint64_t framesCompleted = 0;
    uint64_t framesSampled = 0;
    uint64_t framesFailed = 0;
    uint64_t framesDropped = 0;
    uint64_t decodeErrors = 0;
    uint64_t crcErrors = 0;
    uint64_t protocolErrors = 0;
    uint64_t levelRejects = 0;
    uint64_t validationRejects = 0;
};

struct FrameRecord
{
    uint64_t connectionGeneration = 0;
    uint64_t submitQpc = 0;
    uint64_t completionQpc = 0;
    uint64_t gpuWaitUs = 0;
    uint64_t ageMs = 0;
    uint64_t decodeUs = 0;
    uint64_t drawRecordUs = 0;
    uint64_t fenceValue = 0;
    uint32_t nonClearPixels = 0;
    uint32_t minX = 0;
    uint32_t minY = 0;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
    float depthMin = 1.0f;
    float depthMax = 1.0f;
    bool sampled = false;
    bool visible = false;
    std::string coverage = "GPU_COMPLETED_NOT_READBACK_VERIFIED";
    std::string status = "FAIL";
};

struct PersistentState
{
    GpuOwner gpu;
    std::string runId;
    fs::path runDirectory;
    fs::path runManifestPath;
    std::string runManifestSha256;
    std::string mode;
    uint64_t sampleEvery = 1;
    uint64_t fenceValue = 1;
    uint32_t width = 0;
    uint32_t height = 0;
    bool targetsCreated = false;
    uint32_t colorPitch = 0;
    uint32_t depthPitch = 0;
    uint64_t colorTotal = 0;
    uint64_t depthTotal = 0;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    uint32_t baseVertex = 0;
    xraylevel12::Geometry geometry;
    PersistentCounters counters;
    std::vector<FrameRecord> records;
    std::set<uint64_t> distinctProjections;
    std::set<uint64_t> distinctPositions;
    std::ofstream framesOut;
    std::ofstream countersOut;
    std::ofstream sampledManifestOut;
    bool sampledManifestClosedOk = true;
    uint64_t sampledVisible = 0;
    uint64_t sampledReadback = 0;
    std::vector<std::pair<uint32_t, uint32_t>> sampledDimensions;
    uint64_t renderFailures = 0;
    uint64_t evidenceFailures = 0;
    bool evidenceOk = true;
    bool readbackAvailable = false;
    bool framesClosedOk = true;
    bool countersClosedOk = true;
    std::string evidenceFirstFailure;
    double ageSumMs = 0.0;
    uint64_t ageSamples = 0;
    double waitSumUs = 0.0;
    uint64_t waitSamples = 0;
};

fs::path g_outputForSampling;
xraylevel12::Geometry g_geometryForVisibility;
uint64_t g_deniedAtStart = 0;
bool g_debugLayerAvailable = false;
bool g_gbvRequested = false;

void MarkEvidenceFailure(PersistentState& state, const char* what)
{
    ++state.evidenceFailures;
    if (state.evidenceOk)
        state.evidenceFirstFailure = what != nullptr ? what : "unknown";
    state.evidenceOk = false;
}

bool PublishEvidenceFile(const fs::path& temp, const fs::path& final)
{
    std::error_code ec;
    fs::remove(final, ec);
    ec.clear();
    fs::rename(temp, final, ec);
    if (ec)
        return false;
    return fs::is_regular_file(final, ec) && !ec &&
           fs::file_size(final, ec) > 0 && !ec;
}

bool ReadAllText(const fs::path& path, std::string& text);
bool ContainsLine(const std::string& text, const std::string& line);

bool WriteTextEvidence(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    size_t writeSize = text.size();
    if (FaultRequested("metadata_partial"))
        writeSize /= 2;
    out.write(text.data(), static_cast<std::streamsize>(writeSize));
    if (FaultRequested("metadata_flush"))
    {
        out.flush();
        out.setstate(std::ios::badbit);
    }
    else
    {
        out.flush();
    }
    if (FaultRequested("metadata_close"))
    {
        out.close();
        out.setstate(std::ios::badbit);
    }
    else
    {
        out.close();
    }
    if (!out)
        return false;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string actual;
    char buffer[4096];
    for (;;)
    {
        in.read(buffer, sizeof(buffer));
        const std::streamsize count = in.gcount();
        if (count > 0)
            actual.append(buffer, static_cast<size_t>(count));
        if (in.bad())
            return false;
        if (in.eof())
            break;
        if (in.fail())
            return false;
    }
    return actual == text;
}

std::string Sha256String(const std::string& text)
{
    std::vector<uint8_t> bytes(text.begin(), text.end());
    return b3b1::Sha256Hex(bytes);
}

bool Sha256File(const fs::path& path, std::string& hex)
{
    std::vector<uint8_t> bytes;
    std::string reason;
    if (!ReadFileBytes(path, bytes, reason))
        return false;
    hex = b3b1::Sha256Hex(bytes);
    return true;
}

std::string UtcNowText()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (gmtime_s(&utc, &now) != 0)
        return "19700101T000000Z";
    char buffer[32] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc) == 0)
        return "19700101T000000Z";
    return buffer;
}

std::string MakeRunId()
{
    uint64_t a = 0;
    uint64_t b = 0;
    try
    {
        std::random_device random;
        a = (static_cast<uint64_t>(random()) << 32) ^ random();
        b = (static_cast<uint64_t>(random()) << 32) ^ random();
    }
    catch (...)
    {
        a = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()) ^
            (static_cast<uint64_t>(::GetCurrentProcessId()) << 24);
        b = reinterpret_cast<uintptr_t>(&a);
    }
    if (a == 0 && b == 0)
        b = 1;
    std::ostringstream out;
    out << UtcNowText() << "-" << std::hex << std::setfill('0')
        << std::setw(16) << a << std::setw(16) << b;
    return out.str();
}

bool IsLegacyOutputRoot(const fs::path& base, std::string& reason)
{
    const char* names[] = {
        "summary.txt", "summary.txt.tmp", "frames.csv",
        "persistent-counters.txt", "gpu-log.txt", "geometry-audit.txt",
        "camera-audit.txt", "readback-audit.txt", "gpu-validation.txt",
        "infoqueue-dump.txt", "run-manifest.txt", "sampled"};
    for (const char* name : names)
    {
        std::error_code ec;
        if (fs::exists(base / name, ec) && !ec)
        {
            reason = std::string("legacy evidence artifact at output base: ") +
                     name + "; use a fresh --output base for run directories";
            return true;
        }
    }
    return false;
}

bool CreateRunIdentity(const fs::path& base, RunIdentity& run,
                       std::string& error)
{
    std::error_code ec;
    fs::path absoluteBase = fs::absolute(base, ec);
    if (ec)
        absoluteBase = base;
    fs::create_directories(absoluteBase, ec);
    if (ec)
    {
        error = "cannot create output base: " + ec.message();
        return false;
    }
    if (IsLegacyOutputRoot(absoluteBase, error))
        return false;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        if (FaultRequested("run_dir_create"))
        {
            error = "test fault: run directory creation";
            return false;
        }
        RunIdentity candidate;
        candidate.id = MakeRunId();
        candidate.startedUtc = UtcNowText();
        candidate.base = absoluteBase;
        candidate.directory = absoluteBase / ("run-" + candidate.id);
        candidate.manifest = candidate.directory / "run-manifest.txt";
        if (fs::create_directory(candidate.directory, ec))
        {
            run = candidate;
            return true;
        }
        if (ec)
        {
            error = "cannot create run directory: " + ec.message();
            return false;
        }
    }
    error = "cannot allocate unique run directory";
    return false;
}

bool WriteRunManifest(RunIdentity& run, const std::string& mode,
                      uint64_t sampleEvery, std::string& manifestSha256,
                      std::string& error)
{
    std::ostringstream text;
    text << "run_id=" << run.id << "\n"
         << "pid=" << ::GetCurrentProcessId() << "\n"
         << "started_utc=" << run.startedUtc << "\n"
         << "output_base=" << PathToUtf8(run.base) << "\n"
         << "run_directory=" << PathToUtf8(run.directory) << "\n"
         << "mode=" << mode << "\n"
         << "sample_every=" << sampleEvery << "\n";
    const std::string content = text.str();
    if (FaultRequested("run_manifest_open"))
    {
        error = "test fault: run manifest open";
        return false;
    }
    std::ofstream out(run.manifest, std::ios::binary);
    if (!out)
    {
        error = "cannot open run manifest";
        return false;
    }
    if (FaultRequested("run_manifest_write"))
    {
        out.setstate(std::ios::badbit);
        error = "test fault: run manifest write";
        return false;
    }
    out << content;
    out.flush();
    if (FaultRequested("run_manifest_close"))
    {
        out.close();
        out.setstate(std::ios::badbit);
    }
    else
    {
        out.close();
    }
    if (!out)
    {
        error = "cannot write run manifest";
        return false;
    }
    std::string actual;
    if (!ReadAllText(run.manifest, actual) || actual != content)
    {
        error = "run manifest verification failed";
        return false;
    }
    if (FaultRequested("run_manifest_missing"))
    {
        std::error_code ec;
        fs::remove(run.manifest, ec);
        error = "test fault: run manifest missing";
        return false;
    }
    manifestSha256 = Sha256String(content);
    return true;
}

uint64_t PersistentQpcNow() noexcept
{
    LARGE_INTEGER value{};
    ::QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
}

uint64_t PersistentQpcFrequency() noexcept
{
    static const uint64_t frequency = []() {
        LARGE_INTEGER value{};
        ::QueryPerformanceFrequency(&value);
        return static_cast<uint64_t>(value.QuadPart);
    }();
    return frequency;
}

uint64_t PersistentQpcUs(uint64_t ticks) noexcept
{
    const uint64_t frequency = PersistentQpcFrequency();
    return frequency == 0 ? 0 : (ticks * 1000000ull) / frequency;
}

uint64_t PersistentAgeMs(uint64_t publishQpc) noexcept
{
    const uint64_t now = PersistentQpcNow();
    if (publishQpc == 0 || now <= publishQpc)
        return 0;
    const uint64_t frequency = PersistentQpcFrequency();
    return frequency == 0 ? 0 : ((now - publishQpc) * 1000ull) / frequency;
}

uint64_t HashBytes(const void* data, size_t size) noexcept
{
    uint64_t hash = 1469598103934665603ull;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t HashMatrix(const DirectX::XMFLOAT4X4& matrix) noexcept
{
    return HashBytes(&matrix, sizeof(matrix));
}

uint64_t HashPosition(const DirectX::XMFLOAT3& position) noexcept
{
    return HashBytes(&position, sizeof(position));
}

bool ParsePersistentOptions(const std::vector<std::string>& args,
                            PersistentOptions& options, std::string& reason)
{
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string& arg = args[i];
        auto next = [&](std::string& value) -> bool {
            if (i + 1 >= args.size())
                return false;
            value = args[++i];
            return true;
        };
        std::string value;
        if (arg == "--level")
        {
            if (!next(value)) { reason = "missing --level"; return false; }
            options.level = PathFromUtf8(value);
            if (options.level.empty()) { reason = "invalid --level UTF-8"; return false; }
        }
        else if (arg == "--geom")
        {
            if (!next(value)) { reason = "missing --geom"; return false; }
            options.geom = PathFromUtf8(value);
            if (options.geom.empty()) { reason = "invalid --geom UTF-8"; return false; }
        }
        else if (arg == "--output")
        {
            if (!next(value)) { reason = "missing --output"; return false; }
            options.output = PathFromUtf8(value);
            if (options.output.empty()) { reason = "invalid --output UTF-8"; return false; }
        }
        else if (arg == "--offline-fixture")
        {
            if (!next(value)) { reason = "missing --offline-fixture"; return false; }
            const fs::path fixture = PathFromUtf8(value);
            if (fixture.empty()) { reason = "invalid --offline-fixture UTF-8"; return false; }
            options.offlineFixtures.emplace_back(fixture);
            options.offline = true;
        }
        else if (arg == "--pipe")
        {
            if (!next(value)) { reason = "missing --pipe"; return false; }
            options.pipeName = WideFromUtf8(value);
            if (options.pipeName.empty()) { reason = "invalid --pipe"; return false; }
        }
        else if (arg == "--visual")
        {
            if (!next(value)) { reason = "missing --visual"; return false; }
            options.visual = static_cast<uint32_t>(std::stoul(value));
        }
        else if (arg == "--level-name")
        {
            if (!next(value)) { reason = "missing --level-name"; return false; }
            options.levelName = value;
        }
        else if (arg == "--max-frames")
        {
            if (!next(value)) { reason = "missing --max-frames"; return false; }
            options.maxFrames = static_cast<uint32_t>(std::stoul(value));
        }
        else if (arg == "--duration-ms")
        {
            if (!next(value)) { reason = "missing --duration-ms"; return false; }
            options.durationMs = static_cast<uint32_t>(std::stoul(value));
        }
        else if (arg == "--sample-every")
        {
            if (!next(value)) { reason = "missing --sample-every"; return false; }
            options.sampleEvery = static_cast<uint32_t>(std::stoul(value));
        }
        else
        {
            reason = "unknown argument " + arg;
            return false;
        }
    }
    if (options.level.empty() || options.geom.empty() || options.output.empty())
    {
        reason = "--level, --geom and --output are required";
        return false;
    }
    if (!options.offline && options.pipeName.empty())
    {
        reason = "--pipe is required for live mode";
        return false;
    }
    if (options.visual == 0)
    {
        reason = "invalid visual";
        return false;
    }
    return true;
}

bool LoadJupiterGeometry(const PersistentOptions& options,
                         xraylevel12::Geometry& geometry, Audit& audit)
{
    std::vector<uint8_t> levelBytes;
    std::vector<uint8_t> geomBytes;
    std::string reason;
    if (!ReadFileBytes(options.level, levelBytes, reason) ||
        !ReadFileBytes(options.geom, geomBytes, reason))
    {
        Log(audit.gpu, "%s", reason.c_str());
        return false;
    }
    const xraylevel12::ReadError error = xraylevel12::ReadVisual(
        levelBytes, geomBytes, options.visual, geometry, reason);
    if (error != xraylevel12::ReadError::None)
    {
        Log(audit.gpu, "ReadVisual failed error=%d reason=%s",
            static_cast<int>(error), reason.c_str());
        return false;
    }
    if (!ValidateGeometry(geometry, false, reason))
    {
        Log(audit.gpu, "geometry validation failed: %s", reason.c_str());
        return false;
    }
    if (options.visual == 6470 &&
        (geometry.vertex_count != 3364 || geometry.index_count != 7110 ||
         geometry.triangle_count != 2370))
    {
        Log(audit.gpu, "visual 6470 shape mismatch: %u/%u/%u",
            geometry.vertex_count, geometry.index_count, geometry.triangle_count);
        return false;
    }
    Log(audit.geometry, "visual_id=%u", geometry.visual_id);
    Log(audit.geometry, "shader_id=%u", geometry.shader_id);
    Log(audit.geometry, "vertex_count=%u", geometry.vertex_count);
    Log(audit.geometry, "index_count=%u", geometry.index_count);
    Log(audit.geometry, "triangle_count=%u", geometry.triangle_count);
    Log(audit.geometry, "vertex_stride=%u", geometry.vertex_stride);
    Log(audit.geometry, "first_index=%u base_vertex=%u",
        geometry.first_index, geometry.base_vertex);
    Log(audit.geometry, "vb_bytes=%llu ib_bytes=%llu",
        static_cast<unsigned long long>(geometry.vertex_bytes.size()),
        static_cast<unsigned long long>(geometry.indices.size() * sizeof(uint16_t)));
    Log(audit.geometry, "bounds_min=%.9g %.9g %.9g",
        geometry.bounds.minimum[0], geometry.bounds.minimum[1],
        geometry.bounds.minimum[2]);
    Log(audit.geometry, "bounds_max=%.9g %.9g %.9g",
        geometry.bounds.maximum[0], geometry.bounds.maximum[1],
        geometry.bounds.maximum[2]);
    Log(audit.geometry, "draw=DrawIndexedInstanced(%u,1,%u,%u,0)",
        geometry.index_count, geometry.first_index, geometry.base_vertex);
    Log(audit.geometry, "position_offset=%u position_type=FLOAT3", g_positionOffset);
    return true;
}

bool CreatePersistentDevice(PersistentState& state, Audit& audit)
{
    GpuOwner& owner = state.gpu;
    ComPtr<ID3D12Debug> debug;
    bool debugAvailable = false;
    bool gbvRequested = false;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
    {
        debug->EnableDebugLayer();
        debugAvailable = true;
        Log(audit.validation, "debug_layer=enabled");
        ComPtr<ID3D12Debug3> debug3;
        if (SUCCEEDED(debug.As(&debug3)))
        {
            gbvRequested = true;
            debug3->SetEnableGPUBasedValidation(TRUE);
            Log(audit.validation, "gpu_based_validation_requested=1");
        }
    }
    else
    {
        Log(audit.validation, "debug_layer=unavailable");
    }
    Log(audit.validation, "debug_layer_available=%d", debugAvailable ? 1 : 0);
    Log(audit.validation, "gbv_requested=%d", gbvRequested ? 1 : 0);
    g_debugLayerAvailable = debugAvailable;
    g_gbvRequested = gbvRequested;

    HRESULT result = D3D12CreateDevice(
        nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(owner.device.GetAddressOf()));
    if (FAILED(result))
    {
        Log(audit.gpu, "D3D12CreateDevice failed 0x%08X",
            static_cast<unsigned>(result));
        return false;
    }
    ++state.counters.deviceCreateCount;
    LogAdapterInfo(audit);
    Log(audit.validation, "device_removal_reason=0x%08X",
        static_cast<unsigned>(owner.device->GetDeviceRemovedReason()));

    uint64_t deniedAtStart = 0;
    {
        ComPtr<ID3D12InfoQueue> queue;
        if (SUCCEEDED(owner.device->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf()))))
        {
            deniedAtStart = queue->GetNumMessagesDeniedByStorageFilter();
            queue->ClearStorageFilter();
            Log(audit.validation, "infoqueue_storage_filter=cleared");
            Log(audit.validation, "infoqueue_denied_at_start=%llu",
                static_cast<unsigned long long>(deniedAtStart));
        }
    }
    g_deniedAtStart = deniedAtStart;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!Check(owner.device->CreateCommandQueue(
                   &queueDesc, IID_PPV_ARGS(owner.queue.GetAddressOf())),
               "CreateCommandQueue", audit))
        return false;
    ++state.counters.queueCreateCount;
    if (!Check(owner.device->CreateFence(
                   0, D3D12_FENCE_FLAG_NONE,
                   IID_PPV_ARGS(owner.fence.GetAddressOf())),
               "CreateFence", audit))
        return false;
    owner.fenceEvent.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!owner.fenceEvent)
        return false;
    if (!Check(owner.device->CreateCommandAllocator(
                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                   IID_PPV_ARGS(owner.allocator.GetAddressOf())),
               "CreateCommandAllocator", audit))
        return false;
    if (!Check(owner.device->CreateCommandList(
                   0, D3D12_COMMAND_LIST_TYPE_DIRECT, owner.allocator.Get(),
                   nullptr, IID_PPV_ARGS(owner.list.GetAddressOf())),
               "CreateCommandList", audit))
        return false;
    if (!Check(owner.list->Close(), "Close initial command list", audit))
    {
        owner.listState = ListStateAfterFailedClose();
        return false;
    }
    owner.listState = ListStateAfterClose();
    owner.workState = GpuWorkState::NoWorkSubmitted;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    if (!Check(owner.device->CreateDescriptorHeap(
                   &rtvDesc, IID_PPV_ARGS(owner.rtvHeap.GetAddressOf())),
               "Create RTV heap", audit))
        return false;
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    if (!Check(owner.device->CreateDescriptorHeap(
                   &dsvDesc, IID_PPV_ARGS(owner.dsvHeap.GetAddressOf())),
               "Create DSV heap", audit))
        return false;
    return true;
}

bool SubmitPersistent(PersistentState& state, Audit& audit, const char* stage)
{
    GpuOwner& owner = state.gpu;
    if (!MaySubmit(owner.listState))
        return false;
    const HRESULT close = owner.list->Close();
    if (FAILED(close))
    {
        owner.listState = ListStateAfterFailedClose();
        Log(audit.gpu, "%s Close failed 0x%08X", stage,
            static_cast<unsigned>(close));
        return false;
    }
    owner.listState = ListStateAfterClose();
    ID3D12CommandList* lists[] = {owner.list.Get()};
    owner.queue->ExecuteCommandLists(1, lists);
    owner.workState = StateAfterExecute();

    const uint64_t expected = state.fenceValue;
    const HRESULT signal = owner.queue->Signal(owner.fence.Get(), expected);
    bool arm = false;
    bool waitSignaled = false;
    if (SUCCEEDED(signal))
    {
        if (owner.fence->GetCompletedValue() >= expected)
        {
            arm = true;
            waitSignaled = true;
        }
        else
        {
            arm = SUCCEEDED(owner.fence->SetEventOnCompletion(
                expected, owner.fenceEvent.get()));
            if (arm)
                waitSignaled = ::WaitForSingleObject(owner.fenceEvent.get(),
                                                     kPersistentFenceTimeoutMs) ==
                               WAIT_OBJECT_0;
        }
    }
    const uint64_t completed = owner.fence->GetCompletedValue();
    const long removed = static_cast<long>(owner.device->GetDeviceRemovedReason());
    const b3b2b::FenceEvidence evidence = b3b2b::EvaluateFence(
        SUCCEEDED(signal), arm, waitSignaled, completed, expected, removed);
    Log(audit.gpu,
        "fence stage=%s expected=%llu completed=%llu signal=%d arm=%d wait=%d "
        "removed=0x%08X outcome=%s confirmed=%d",
        stage, static_cast<unsigned long long>(expected),
        static_cast<unsigned long long>(completed), SUCCEEDED(signal) ? 1 : 0,
        arm ? 1 : 0, waitSignaled ? 1 : 0, static_cast<unsigned>(removed),
        evidence.reason, evidence.confirmed ? 1 : 0);
    if (!evidence.confirmed)
    {
        owner.workState = b3b2b::ClassifyFenceFailure(removed) ==
                                  b3b2b::FenceFailureDisposition::DeviceRemoved
                              ? GpuWorkState::DeviceRemoved
                              : GpuWorkState::WorkSubmittedUnknown;
        return false;
    }
    owner.workState = StateAfterFenceWait(FenceOutcome::Completed, removed);
    owner.listState = ListStateAfterConfirmedSubmit();
    ++state.fenceValue;
    return true;
}

bool ResetPersistent(PersistentState& state, Audit& audit)
{
    GpuOwner& owner = state.gpu;
    if (!MayResetList(owner.listState, owner.workState))
    {
        Log(audit.gpu, "reset rejected list=%d work=%d",
            static_cast<int>(owner.listState), static_cast<int>(owner.workState));
        return false;
    }
    if (FAILED(owner.allocator->Reset()))
        return false;
    if (FAILED(owner.list->Reset(owner.allocator.Get(), nullptr)))
    {
        owner.listState = ListStateAfterFailedClose();
        return false;
    }
    owner.listState = ListStateAfterReset();
    owner.workState = GpuWorkState::NoWorkSubmitted;
    return true;
}

bool EnsurePersistentTargets(PersistentState& state, uint32_t targetWidth,
                             uint32_t targetHeight, Audit& audit)
{
    if (targetWidth == 0 || targetHeight == 0)
        return false;
    if (state.width == targetWidth && state.height == targetHeight)
        return true;
    if (state.width != 0 && !b3b2b::NeedsTargetRecreate(
                                state.width, state.height, targetWidth,
                                targetHeight,
                                CompletionConfirmed(state.gpu.workState)))
    {
        Log(audit.gpu, "viewport change rejected");
        return false;
    }
    GpuOwner& owner = state.gpu;
    owner.color.Reset();
    owner.depth.Reset();
    owner.colorReadback.Reset();
    owner.depthReadback.Reset();
    state.width = targetWidth;
    state.height = targetHeight;
    state.readbackAvailable = true;

    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorClear.Color[0] = 0.05f;
    colorClear.Color[1] = 0.06f;
    colorClear.Color[2] = 0.08f;
    colorClear.Color[3] = 1.0f;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    if (!CreateCommitted(owner.device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         TextureDesc(state.width, state.height,
                                     DXGI_FORMAT_R8G8B8A8_UNORM),
                         D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
                         owner.color.GetAddressOf(), audit, "color target"))
        return false;
    if (!CreateCommitted(owner.device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         TextureDesc(state.width, state.height,
                                     DXGI_FORMAT_D32_FLOAT),
                         D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                         owner.depth.GetAddressOf(), audit, "depth target"))
        return false;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        owner.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        owner.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    owner.device->CreateRenderTargetView(owner.color.Get(), nullptr, rtv);
    owner.device->CreateDepthStencilView(owner.depth.Get(), nullptr, dsv);

    D3D12_RESOURCE_DESC colorDesc = owner.color->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorFootprint{};
    UINT colorRows = 0;
    UINT64 colorRowSize = 0;
    uint64_t colorTotal = 0;
    owner.device->GetCopyableFootprints(&colorDesc, 0, 1, 0, &colorFootprint,
                                        &colorRows, &colorRowSize, &colorTotal);
    state.colorPitch = colorFootprint.Footprint.RowPitch;
    bool readbackOk = !FaultRequested("readback_create");
    if (readbackOk)
        readbackOk = CreateReadbackBuffer(owner.device.Get(), colorTotal,
                                          owner.colorReadback.GetAddressOf(),
                                          audit);
    D3D12_RESOURCE_DESC depthDesc = owner.depth->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT depthFootprint{};
    UINT depthRows = 0;
    UINT64 depthRowSize = 0;
    uint64_t depthTotal = 0;
    owner.device->GetCopyableFootprints(&depthDesc, 0, 1, 0, &depthFootprint,
                                        &depthRows, &depthRowSize, &depthTotal);
    state.depthPitch = depthFootprint.Footprint.RowPitch;
    if (readbackOk)
        readbackOk = CreateReadbackBuffer(owner.device.Get(), depthTotal,
                                          owner.depthReadback.GetAddressOf(),
                                          audit);
    state.colorTotal = colorTotal;
    state.depthTotal = depthTotal;
    if (!readbackOk)
    {
        owner.colorReadback.Reset();
        owner.depthReadback.Reset();
        state.readbackAvailable = false;
        MarkEvidenceFailure(state, "readback_create");
    }
    if (state.targetsCreated)
        ++state.counters.renderTargetRecreateCount;
    state.targetsCreated = true;
    return true;
}

bool RenderPersistentFrame(PersistentState& state,
                           const gamecamera12::GameCameraSnapshot12& camera,
                           const cameracapture12::CameraCaptureFile12& capture,
                           const b3b2::CameraFrameMeta& meta, uint64_t generation,
                           bool sample, uint64_t sampleEvery, Audit& audit,
                           FrameRecord& record)
{
    GpuOwner& owner = state.gpu;
    const uint64_t recordStart = PersistentQpcNow();
    if (!EnsurePersistentTargets(state, camera.ViewportWidth(),
                                 camera.ViewportHeight(), audit))
        return false;
    if (!ResetPersistent(state, audit))
        return false;
    if (!owner.constants)
    {
        if (!CreateCommitted(owner.device.Get(), Heap(D3D12_HEAP_TYPE_UPLOAD),
                             BufferDesc(256), D3D12_RESOURCE_STATE_GENERIC_READ,
                             nullptr, owner.constants.GetAddressOf(), audit,
                             "constant buffer"))
            return false;
    }
    void* mapped = nullptr;
    if (FAILED(owner.constants->Map(0, nullptr, &mapped)))
        return false;
    std::memcpy(mapped, &camera.ViewProjection(), sizeof(DirectX::XMFLOAT4X4));
    owner.constants->Unmap(0, nullptr);

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(state.width),
                            static_cast<float>(state.height), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(state.width),
                       static_cast<LONG>(state.height)};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        owner.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        owner.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    const float clearColor[4] = {0.05f, 0.06f, 0.08f, 1.0f};
    owner.list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    owner.list->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    owner.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                                      nullptr);
    owner.list->RSSetViewports(1, &viewport);
    owner.list->RSSetScissorRects(1, &scissor);
    owner.list->SetGraphicsRootSignature(owner.rootSignature.Get());
    owner.list->SetPipelineState(owner.pipelineState.Get());
    owner.list->SetGraphicsRootConstantBufferView(
        0, owner.constants->GetGPUVirtualAddress());
    owner.list->IASetVertexBuffers(0, 1, &state.vertexView);
    owner.list->IASetIndexBuffer(&state.indexView);
    owner.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    owner.list->DrawIndexedInstanced(state.indexCount, 1, state.firstIndex,
                                     state.baseVertex, 0);
    const uint64_t drawEnd = PersistentQpcNow();

    if (sample && state.readbackAvailable)
    {
        Transition(owner.list.Get(), owner.color.Get(),
                   D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(owner.list.Get(), owner.depth.Get(),
                   D3D12_RESOURCE_STATE_DEPTH_WRITE,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (!CopyTextureToReadback(owner.device.Get(), owner.list.Get(),
                                   owner.color.Get(),
                                   owner.colorReadback.Get(), state.width,
                                   state.height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                   state.colorPitch, state.colorTotal, audit))
            return false;
        if (!CopyTextureToReadback(owner.device.Get(), owner.list.Get(),
                                   owner.depth.Get(),
                                   owner.depthReadback.Get(), state.width,
                                   state.height, DXGI_FORMAT_D32_FLOAT,
                                   state.depthPitch, state.depthTotal, audit))
            return false;
        Transition(owner.list.Get(), owner.color.Get(),
                   D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(owner.list.Get(), owner.depth.Get(),
                   D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    record.connectionGeneration = generation;
    record.submitQpc = PersistentQpcNow();
    record.drawRecordUs = PersistentQpcUs(drawEnd - recordStart);
    record.ageMs = meta.publishQpc != 0 ? PersistentAgeMs(meta.publishQpc) : 0;
    const uint64_t waitStart = PersistentQpcNow();
    if (!SubmitPersistent(state, audit, "camera frame"))
        return false;
    record.gpuWaitUs = PersistentQpcUs(PersistentQpcNow() - waitStart);
    record.completionQpc = PersistentQpcNow();
    record.fenceValue = state.fenceValue - 1;
    record.sampled = sample;
    ++state.counters.framesSubmitted;
    ++state.counters.framesCompleted;
    state.ageSumMs += static_cast<double>(record.ageMs);
    ++state.ageSamples;
    state.waitSumUs += static_cast<double>(record.gpuWaitUs);
    ++state.waitSamples;

    if (!sample)
    {
        record.coverage = "GPU_COMPLETED_NOT_READBACK_VERIFIED";
        record.status = "GPU_COMPLETED_NOT_READBACK_VERIFIED";
        return true;
    }

    ++state.counters.framesSampled;
    if (!state.readbackAvailable)
    {
        MarkEvidenceFailure(state, "readback_unavailable");
        record.coverage = "EVIDENCE_UNAVAILABLE";
        record.status = b3b2b::SampleEvidenceStatus(true, false, false, false);
        return true;
    }
    void* mappedColor = nullptr;
    if (FaultRequested("color_map") ||
        FAILED(owner.colorReadback->Map(0, nullptr, &mappedColor)))
    {
        MarkEvidenceFailure(state, "color_map");
        record.coverage = "EVIDENCE_UNAVAILABLE";
        record.status = b3b2b::SampleEvidenceStatus(true, false, false, false);
        return true;
    }
    std::vector<uint8_t> colorPixels(
        static_cast<size_t>(state.colorPitch) * state.height);
    std::memcpy(colorPixels.data(), mappedColor, colorPixels.size());
    owner.colorReadback->Unmap(0, nullptr);
    void* mappedDepth = nullptr;
    if (FaultRequested("depth_map") ||
        FAILED(owner.depthReadback->Map(0, nullptr, &mappedDepth)))
    {
        MarkEvidenceFailure(state, "depth_map");
        record.coverage = "EVIDENCE_UNAVAILABLE";
        record.status = b3b2b::SampleEvidenceStatus(true, false, false, false);
        return true;
    }
    const uint32_t depthRowFloats = state.depthPitch / sizeof(float);
    std::vector<float> depthValues(
        static_cast<size_t>(depthRowFloats) * state.height);
    std::memcpy(depthValues.data(), mappedDepth,
                depthValues.size() * sizeof(float));
    owner.depthReadback->Unmap(0, nullptr);
    ++state.sampledReadback;
    state.sampledDimensions.emplace_back(state.width, state.height);

    uint32_t nonClear = 0;
    uint32_t minX = state.width;
    uint32_t minY = state.height;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
    for (uint32_t y = 0; y < state.height; ++y)
    {
        const uint8_t* row =
            colorPixels.data() + static_cast<size_t>(y) * state.colorPitch;
        for (uint32_t x = 0; x < state.width; ++x)
        {
            if (row[x * 4] != 13 || row[x * 4 + 1] != 15 ||
                row[x * 4 + 2] != 20)
            {
                ++nonClear;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    float depthMin = 1.0f;
    float depthMax = 0.0f;
    for (uint32_t y = 0; y < state.height; ++y)
    {
        const float* row =
            depthValues.data() + static_cast<size_t>(y) * depthRowFloats;
        for (uint32_t x = 0; x < state.width; ++x)
        {
            depthMin = std::min(depthMin, row[x]);
            depthMax = std::max(depthMax, row[x]);
        }
    }
    record.nonClearPixels = nonClear;
    record.minX = minX;
    record.minY = minY;
    record.maxX = maxX;
    record.maxY = maxY;
    record.depthMin = depthMin;
    record.depthMax = depthMax;

    std::string visibilityReason;
    const CpuVisibility visibility =
        AnalyzeVisibility(g_geometryForVisibility, camera.ViewProjection(),
                          state.width, state.height, visibilityReason);
    const bool depthAllClear = depthMin == 1.0f && depthMax == 1.0f;
    const bool depthWritten = depthMin < 1.0f && depthMin >= 0.0f &&
                              depthMax <= 1.0f;
    const bool gpuPixels = nonClear != 0 && depthWritten;
    const bool guaranteed =
        visibility.confidence == b3b1::CoverageConfidence::GuaranteedInterior;
    const bool boundary =
        visibility.confidence == b3b1::CoverageConfidence::BoundaryOnly;
    const bool noCoverage =
        visibility.confidence == b3b1::CoverageConfidence::NoSampleCoverage;
    const bool outside =
        visibility.confidence == b3b1::CoverageConfidence::OutsideFrustum;
    if (guaranteed)
    {
        if (!gpuPixels)
            record.coverage = "RENDER_FAILURE";
        else
            record.coverage = "VISIBLE_DRAW_VERIFIED";
    }
    else if (boundary)
    {
        record.coverage = gpuPixels ? "VISIBLE_DRAW_VERIFIED"
                                    : "VISIBILITY_NOT_VERIFIED";
    }
    else if (noCoverage)
    {
        record.coverage = (nonClear == 0 && depthAllClear)
                              ? "NO_PIXEL_COVERAGE"
                              : "VISIBILITY_NOT_VERIFIED";
    }
    else if (outside)
    {
        record.coverage = (nonClear == 0 && depthAllClear)
                              ? "OUT_OF_FRUSTUM"
                              : "VISIBILITY_NOT_VERIFIED";
    }
    else
    {
        record.coverage = "VISIBILITY_NOT_VERIFIED";
    }

    const fs::path sampledDir = g_outputForSampling / "sampled";
    std::error_code ec;
    fs::create_directories(sampledDir, ec);
    const std::string stem = "frame-" + std::to_string(state.sampledReadback);
    const fs::path finalColor = sampledDir / (stem + "-color.ppm");
    const fs::path finalDepth = sampledDir / (stem + "-depth.pgm");
    const fs::path finalMeta = sampledDir / (stem + "-meta.txt");

    bool colorOk = true;
    if (FaultRequested("image_create"))
    {
        fs::create_directories(finalColor, ec);
        colorOk = false;
    }
    else
    {
        fs::path tempColor = finalColor;
        tempColor += ".tmp";
        colorOk = WritePpm(tempColor, state.width, state.height, colorPixels,
                           state.colorPitch);
        if (colorOk && FaultRequested("image_short"))
        {
            const uint64_t size = fs::file_size(tempColor, ec);
            if (!ec)
                fs::resize_file(tempColor, size / 2, ec);
            PublishEvidenceFile(tempColor, finalColor);
            colorOk = false;
        }
        else if (colorOk)
        {
            colorOk = PublishEvidenceFile(tempColor, finalColor);
        }
    }
    if (!colorOk)
        MarkEvidenceFailure(state, "image_color");

    bool depthOk = true;
    if (FaultRequested("depth_evidence"))
    {
        fs::create_directories(finalDepth, ec);
        depthOk = false;
    }
    else
    {
        fs::path tempDepth = finalDepth;
        tempDepth += ".tmp";
        depthOk = WriteDepthPgm(tempDepth, state.width, state.height,
                                depthValues, state.depthPitch);
        if (depthOk && FaultRequested("depth_short"))
        {
            const uint64_t size = fs::file_size(tempDepth, ec);
            if (!ec)
                fs::resize_file(tempDepth, size / 2, ec);
            PublishEvidenceFile(tempDepth, finalDepth);
            depthOk = false;
        }
        else if (depthOk)
        {
            depthOk = PublishEvidenceFile(tempDepth, finalDepth);
        }
    }
    if (!depthOk)
        MarkEvidenceFailure(state, "image_depth");

    std::vector<uint8_t> wire;
    cameracapture12::FileError encodeError = cameracapture12::FileError::None;
    if (!cameracapture12::Encode(capture, wire, encodeError))
        wire.clear();
    const std::string sha = wire.empty() ? std::string() : b3b1::Sha256Hex(wire);
    std::ostringstream metaText;
    metaText << "run_id=" << state.runId << "\n"
             << "frame_index=" << state.sampledReadback << "\n"
             << "level_session=" << capture.input.levelSession << "\n"
             << "capture_epoch=" << capture.input.captureEpoch << "\n"
             << "capture_sequence=" << capture.input.captureSequence << "\n"
             << "source_game_frame=" << capture.input.sourceGameFrame << "\n"
             << "camera_hash=" << std::hex << std::setw(16)
             << std::setfill('0') << b3b2::ComputePacketHash(capture)
             << std::dec << std::setfill(' ') << "\n"
             << "camera_sha256=" << sha << "\n"
             << "fence_value=" << record.fenceValue << "\n"
             << "viewport=" << state.width << "x" << state.height << "\n"
             << "non_clear_pixels=" << nonClear << "\n"
             << "bbox=" << minX << "," << minY << ".." << maxX << ","
             << maxY << "\n"
             << "depth_min=" << std::setprecision(9) << depthMin << "\n"
             << "depth_max=" << depthMax << "\n"
             << "cpu_coverage=" << visibility.coveredPixels << "\n"
             << "cpu_confidence=" << ConfidenceName(visibility.confidence)
             << "\n"
             << "gpu_status=GPU_COMPLETED\n"
             << "coverage_status=" << record.coverage << "\n";

    bool metaOk = true;
    if (FaultRequested("metadata_open"))
    {
        fs::create_directories(finalMeta, ec);
        metaOk = false;
    }
    else
    {
        fs::path tempMeta = finalMeta;
        tempMeta += ".tmp";
        metaOk = WriteTextEvidence(tempMeta, metaText.str());
        if (metaOk)
            metaOk = PublishEvidenceFile(tempMeta, finalMeta);
        if (metaOk && FaultRequested("metadata_missing"))
        {
            fs::remove(finalMeta, ec);
            metaOk = false;
        }
    }
    if (!metaOk)
        MarkEvidenceFailure(state, "metadata");

    std::string colorHash;
    std::string depthHash;
    std::string metaHash;
    const bool hashOk = colorOk && depthOk && metaOk &&
                        Sha256File(finalColor, colorHash) &&
                        Sha256File(finalDepth, depthHash) &&
                        Sha256File(finalMeta, metaHash);
    bool sampledManifestOk = true;
    if (!state.sampledManifestOut)
    {
        sampledManifestOk = false;
    }
    else
    {
        state.sampledManifestOut
            << "frame=" << state.sampledReadback << " color_sha256=" << colorHash
            << " depth_sha256=" << depthHash << " meta_sha256=" << metaHash
            << "\n";
        if (!state.sampledManifestOut)
            sampledManifestOk = false;
    }
    if (!hashOk)
        MarkEvidenceFailure(state, "sampled_hash");
    if (!sampledManifestOk)
        MarkEvidenceFailure(state, "sampled_manifest_write");

    const bool frameEvidenceOk =
        colorOk && depthOk && metaOk && hashOk && sampledManifestOk;
    const char* evidenceStatus = b3b2b::SampleEvidenceStatus(
        true, true, frameEvidenceOk, gpuPixels);
    if (std::strcmp(record.coverage.c_str(), "RENDER_FAILURE") == 0)
    {
        ++state.renderFailures;
        record.status = "RENDER_FAILURE";
    }
    else if (std::strcmp(evidenceStatus, "UNVERIFIED") == 0)
    {
        record.status = "UNVERIFIED";
    }
    else
    {
        record.status = record.coverage;
    }
    if (record.status == std::string("VISIBLE_DRAW_VERIFIED"))
    {
        record.visible = true;
        ++state.sampledVisible;
    }

    Log(audit.readback,
        "frame_index=%llu session=%llu epoch=%llu sequence=%llu "
        "viewport=%ux%u non_clear_pixels=%u bbox=%u,%u..%u,%u "
        "depth_min=%.9g depth_max=%.9g cpu_confidence=%s status=%s",
        static_cast<unsigned long long>(state.sampledReadback),
        static_cast<unsigned long long>(capture.input.levelSession),
        static_cast<unsigned long long>(capture.input.captureEpoch),
        static_cast<unsigned long long>(capture.input.captureSequence),
        state.width, state.height, nonClear, minX, minY, maxX, maxY, depthMin,
        depthMax, ConfidenceName(visibility.confidence),
        record.status.c_str());
    Log(audit.camera,
        "frame_index=%llu session=%llu epoch=%llu sequence=%llu "
        "camera_hash=%016llX fence=%llu status=%s",
        static_cast<unsigned long long>(state.sampledReadback),
        static_cast<unsigned long long>(capture.input.levelSession),
        static_cast<unsigned long long>(capture.input.captureEpoch),
        static_cast<unsigned long long>(capture.input.captureSequence),
        static_cast<unsigned long long>(b3b2::ComputePacketHash(capture)),
        static_cast<unsigned long long>(record.fenceValue),
        record.status.c_str());
    if (!audit.readback)
        MarkEvidenceFailure(state, "readback_log");
    if (!audit.camera)
        MarkEvidenceFailure(state, "camera_log");
    (void)sampleEvery;
    return true;
}

bool PreparePersistentRenderer(PersistentState& state,
                               const xraylevel12::Geometry& geometry,
                               Audit& audit)
{
    if (!CreatePersistentDevice(state, audit))
        return false;
    if (!CreateRootSignature(state.gpu.device.Get(),
                             state.gpu.rootSignature.GetAddressOf(), audit))
        return false;
    if (!CreatePipeline(state.gpu.device.Get(), state.gpu.rootSignature.Get(),
                        g_positionOffset, state.gpu.pipelineState.GetAddressOf(),
                        audit))
        return false;
    ++state.counters.psoCreateCount;

    GpuOwner& owner = state.gpu;
    if (!CreateCommitted(owner.device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         BufferDesc(geometry.vertex_bytes.size()),
                         D3D12_RESOURCE_STATE_COMMON, nullptr,
                         owner.vertexBuffer.GetAddressOf(), audit,
                         "vertex buffer"))
        return false;
    ++state.counters.vertexBufferCreateCount;
    if (!CreateCommitted(owner.device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         BufferDesc(geometry.indices.size() * sizeof(uint16_t)),
                         D3D12_RESOURCE_STATE_COMMON, nullptr,
                         owner.indexBuffer.GetAddressOf(), audit, "index buffer"))
        return false;
    ++state.counters.indexBufferCreateCount;
    if (!ResetPersistent(state, audit))
        return false;
    if (!CopyBuffer(owner.device.Get(), owner.list.Get(),
                    owner.vertexBuffer.Get(), geometry.vertex_bytes.data(),
                    geometry.vertex_bytes.size(),
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, audit,
                    "vertex upload", owner.uploads))
        return false;
    if (!CopyBuffer(owner.device.Get(), owner.list.Get(),
                    owner.indexBuffer.Get(), geometry.indices.data(),
                    geometry.indices.size() * sizeof(uint16_t),
                    D3D12_RESOURCE_STATE_INDEX_BUFFER, audit, "index upload",
                    owner.uploads))
        return false;
    if (!SubmitPersistent(state, audit, "geometry upload"))
        return false;
    owner.uploads.clear();
    ++state.counters.geometryUploadCount;
    state.vertexView.BufferLocation = owner.vertexBuffer->GetGPUVirtualAddress();
    state.vertexView.SizeInBytes = static_cast<UINT>(geometry.vertex_bytes.size());
    state.vertexView.StrideInBytes = geometry.vertex_stride;
    state.indexView.BufferLocation = owner.indexBuffer->GetGPUVirtualAddress();
    state.indexView.SizeInBytes =
        static_cast<UINT>(geometry.indices.size() * sizeof(uint16_t));
    state.indexView.Format = DXGI_FORMAT_R16_UINT;
    state.indexCount = geometry.index_count;
    state.firstIndex = geometry.first_index;
    state.baseVertex = geometry.base_vertex;
    state.geometry = geometry;
    return true;
}

bool ReadStreamCamera(b3b2::CameraStreamClient& client,
                      b3b2::ReceiverValidationState& validation,
                      PersistentState& state,
                      const PersistentOptions& options, Audit& audit,
                      uint64_t generation, cameracapture12::CameraCaptureFile12& capture,
                      b3b2::CameraFrameMeta& meta, uint64_t& decodeUs,
                      bool& producerShutdown)
{
    (void)audit;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        b3b2::StreamMessage message;
        const b3b2::MessageReadStatus readStatus = client.Read(message, 2000);
        if (readStatus == b3b2::MessageReadStatus::Timeout)
            continue;
        if (readStatus == b3b2::MessageReadStatus::Closed)
            return false;
        if (readStatus != b3b2::MessageReadStatus::Ok)
        {
            ++state.counters.protocolErrors;
            if (readStatus == b3b2::MessageReadStatus::CrcMismatch)
                ++state.counters.crcErrors;
            return false;
        }
        if (message.type == b3b2::StreamMessageType::Hello)
        {
            b3b2::HelloPayload hello{};
            if (!b3b2::ParseHello(message.payload, hello))
            {
                ++state.counters.protocolErrors;
                continue;
            }
            std::string reason;
            if (!validation.ObserveHello(hello, reason))
            {
                ++state.counters.validationRejects;
                continue;
            }
            continue;
        }
        if (message.type == b3b2::StreamMessageType::InvalidateSession)
        {
            b3b2::InvalidatePayload invalidate{};
            if (!b3b2::ParseInvalidate(message.payload, invalidate))
            {
                ++state.counters.protocolErrors;
                continue;
            }
            std::string reason;
            if (!validation.ObserveInvalidate(invalidate, reason))
                ++state.counters.validationRejects;
            if (invalidate.reason == b3b2::InvalidateReason::Shutdown)
                producerShutdown = true;
            continue;
        }
        if (message.type == b3b2::StreamMessageType::Shutdown)
        {
            producerShutdown = true;
            return false;
        }
        if (message.type != b3b2::StreamMessageType::CameraFrame)
            continue;

        std::span<const uint8_t> codec;
        if (!b3b2::ParseCameraFrame(message.payload, meta, codec))
        {
            ++state.counters.protocolErrors;
            continue;
        }
        const uint64_t decodeStart = PersistentQpcNow();
        cameracapture12::FileError fileError = cameracapture12::FileError::None;
        if (!cameracapture12::Decode(codec, capture, fileError))
        {
            ++state.counters.decodeErrors;
            continue;
        }
        decodeUs = PersistentQpcUs(PersistentQpcNow() - decodeStart);
        if (!LevelNamesEqual(capture.levelName, options.levelName.c_str()))
        {
            ++state.counters.levelRejects;
            continue;
        }
        const gamecamera12::CameraCaptureResult12 result =
            gamecamera12::ValidateCamera12(capture.input);
        if (result.error != gamecamera12::CameraError12::None || !result.camera)
        {
            ++state.counters.validationRejects;
            continue;
        }
        std::string reason;
        if (!validation.ObserveCamera(capture, reason))
        {
            ++state.counters.validationRejects;
            continue;
        }
        (void)generation;
        return true;
    }
    return false;
}

bool WriteFrameCsv(PersistentState& state, const FrameRecord& record,
                   const cameracapture12::CameraCaptureFile12& capture,
                   const b3b2::CameraFrameMeta& meta, const char* kind,
                   uint64_t index, uint64_t generation)
{
    if (FaultRequested("csv_partial"))
    {
        state.framesOut << index << "," << kind << ",partial";
        MarkEvidenceFailure(state, "csv_partial");
        return false;
    }
    if (!state.framesOut)
    {
        MarkEvidenceFailure(state, "csv_open");
        return false;
    }
    const uint64_t hash = b3b2::ComputePacketHash(capture);
    std::vector<uint8_t> wire;
    cameracapture12::FileError error = cameracapture12::FileError::None;
    cameracapture12::Encode(capture, wire, error);
    const std::string sha =
        wire.empty() ? std::string() : b3b1::Sha256Hex(wire);
    state.framesOut
        << state.runId << "," << index << "," << kind << "," << generation << ","
        << capture.input.levelSession << "," << capture.input.captureEpoch << ","
        << capture.input.captureSequence << ","
        << capture.input.sourceGameFrame << "," << std::hex << std::setw(16)
        << std::setfill('0') << hash << std::dec << std::setfill(' ') << ","
        << sha << "," << capture.input.viewportWidth << "x"
        << capture.input.viewportHeight
        << ",\"(" << capture.input.position.x << "," << capture.input.position.y
        << "," << capture.input.position.z << ")\"," << meta.publishQpc << ","
        << record.submitQpc << "," << record.completionQpc << "," << record.ageMs
        << "," << record.decodeUs << "," << record.drawRecordUs << ","
        << record.gpuWaitUs << "," << record.fenceValue << ","
        << (record.sampled ? 1 : 0) << "," << record.coverage << ","
        << record.nonClearPixels << ",\"" << record.minX << "," << record.minY
        << ".." << record.maxX << "," << record.maxY << "\"," << record.depthMin
        << "," << record.depthMax << "," << record.status << "\n";
    if (FaultRequested("csv_flush"))
    {
        state.framesOut.flush();
        state.framesOut.setstate(std::ios::badbit);
    }
    if (!state.framesOut)
    {
        MarkEvidenceFailure(state, "csv_write");
        return false;
    }
    return true;
}

bool WriteCounters(PersistentState& state)
{
    if (FaultRequested("counters_open"))
    {
        MarkEvidenceFailure(state, "counters_open");
        return false;
    }
    if (!state.countersOut)
    {
        MarkEvidenceFailure(state, "counters_open");
        return false;
    }
    Log(state.countersOut, "run_id=%s", state.runId.c_str());
    Log(state.countersOut, "mode=%s", state.mode.c_str());
    Log(state.countersOut, "sample_every=%llu",
        static_cast<unsigned long long>(state.sampleEvery));
    Log(state.countersOut, "device_create_count=%llu",
        static_cast<unsigned long long>(state.counters.deviceCreateCount));
    Log(state.countersOut, "queue_create_count=%llu",
        static_cast<unsigned long long>(state.counters.queueCreateCount));
    Log(state.countersOut, "pso_create_count=%llu",
        static_cast<unsigned long long>(state.counters.psoCreateCount));
    Log(state.countersOut, "geometry_upload_count=%llu",
        static_cast<unsigned long long>(state.counters.geometryUploadCount));
    Log(state.countersOut, "vertex_buffer_create_count=%llu",
        static_cast<unsigned long long>(state.counters.vertexBufferCreateCount));
    Log(state.countersOut, "index_buffer_create_count=%llu",
        static_cast<unsigned long long>(state.counters.indexBufferCreateCount));
    Log(state.countersOut, "render_target_recreate_count=%llu",
        static_cast<unsigned long long>(state.counters.renderTargetRecreateCount));
    Log(state.countersOut, "frames_received=%llu",
        static_cast<unsigned long long>(state.counters.framesReceived));
    Log(state.countersOut, "frames_submitted=%llu",
        static_cast<unsigned long long>(state.counters.framesSubmitted));
    Log(state.countersOut, "frames_completed=%llu",
        static_cast<unsigned long long>(state.counters.framesCompleted));
    Log(state.countersOut, "frames_sampled=%llu",
        static_cast<unsigned long long>(state.counters.framesSampled));
    Log(state.countersOut, "frames_failed=%llu",
        static_cast<unsigned long long>(state.counters.framesFailed));
    state.countersOut.flush();
    if (FaultRequested("counters_close"))
    {
        state.countersOut.close();
        state.countersOut.setstate(std::ios::badbit);
    }
    else
    {
        state.countersOut.close();
    }
    if (!state.countersOut)
    {
        MarkEvidenceFailure(state, "counters_write");
        state.countersClosedOk = false;
        return false;
    }
    state.countersClosedOk = true;
    return true;
}

bool WritePersistentSummary(PersistentState& state, const fs::path& output,
                            const char* acceptance, const char* gpuStatus,
                            const char* mode,
                            const std::string& validationStatus,
                            bool validationPassed, bool evidencePassed,
                            bool visibilityPassed)
{
    (void)output;
    std::ostringstream text;
    std::string summaryRunId = state.runId;
    if (FaultRequested("summary_run_id_mismatch"))
        summaryRunId = "run-id-mismatch";
    text << "run_id=" << summaryRunId << "\n"
         << "run_directory=" << PathToUtf8(state.runDirectory) << "\n"
         << "run_manifest_sha256=" << state.runManifestSha256 << "\n"
         << "mode=" << mode << "\n"
         << "sample_every=" << state.sampleEvery << "\n"
         << "acceptance_status=" << acceptance << "\n"
         << "gpu_status=" << gpuStatus << "\n"
         << "validation_status=" << validationStatus << "\n"
         << "validation_passed=" << (validationPassed ? 1 : 0) << "\n"
         << "evidence_status=" << (evidencePassed ? "PASS" : "FAIL") << "\n"
         << "visibility_status=" << (visibilityPassed ? "PASS" : "UNVERIFIED")
         << "\n"
         << "evidence_failures=" << state.evidenceFailures << "\n"
         << "evidence_first_failure=" << state.evidenceFirstFailure << "\n"
         << "device_create_count=" << state.counters.deviceCreateCount << "\n"
         << "queue_create_count=" << state.counters.queueCreateCount << "\n"
         << "pso_create_count=" << state.counters.psoCreateCount << "\n"
         << "geometry_upload_count=" << state.counters.geometryUploadCount
         << "\n"
         << "vertex_buffer_create_count="
         << state.counters.vertexBufferCreateCount << "\n"
         << "index_buffer_create_count="
         << state.counters.indexBufferCreateCount << "\n"
         << "render_target_recreate_count="
         << state.counters.renderTargetRecreateCount << "\n"
         << "frames_received=" << state.counters.framesReceived << "\n"
         << "frames_submitted=" << state.counters.framesSubmitted << "\n"
         << "frames_completed=" << state.counters.framesCompleted << "\n"
         << "frames_sampled=" << state.counters.framesSampled << "\n"
         << "frames_dropped=" << state.counters.framesDropped << "\n"
         << "distinct_view_projections=" << state.distinctProjections.size()
         << "\n"
         << "distinct_positions=" << state.distinctPositions.size() << "\n"
         << "sampled_visible_frames=" << state.sampledVisible << "\n"
         << "render_failures=" << state.renderFailures << "\n"
         << "crc_errors=" << state.counters.crcErrors << "\n"
         << "decode_errors=" << state.counters.decodeErrors << "\n"
         << "protocol_errors=" << state.counters.protocolErrors << "\n"
         << "level_rejects=" << state.counters.levelRejects << "\n"
         << "validation_rejects=" << state.counters.validationRejects << "\n";
    const double avgAge = state.ageSamples == 0
                              ? 0.0
                              : state.ageSumMs / state.ageSamples;
    const double avgWait = state.waitSamples == 0
                               ? 0.0
                               : state.waitSumUs / state.waitSamples;
    std::vector<double> ages;
    std::vector<double> waits;
    std::vector<double> decode;
    std::vector<double> draw;
    ages.reserve(state.records.size());
    waits.reserve(state.records.size());
    decode.reserve(state.records.size());
    draw.reserve(state.records.size());
    for (const FrameRecord& record : state.records)
    {
        ages.push_back(static_cast<double>(record.ageMs));
        waits.push_back(static_cast<double>(record.gpuWaitUs));
        decode.push_back(static_cast<double>(record.decodeUs));
        draw.push_back(static_cast<double>(record.drawRecordUs));
    }
    auto percentile = [](std::vector<double>& values, double fraction) {
        if (values.empty())
            return 0.0;
        std::sort(values.begin(), values.end());
        const size_t index = static_cast<size_t>(
            (static_cast<double>(values.size()) - 1.0) * fraction);
        return values[std::min(index, values.size() - 1)];
    };
    text << "frame_age_ms_avg=" << avgAge << "\n"
         << "frame_age_ms_p50=" << percentile(ages, 0.50) << "\n"
         << "frame_age_ms_p95=" << percentile(ages, 0.95) << "\n"
         << "frame_age_ms_p99=" << percentile(ages, 0.99) << "\n"
         << "gpu_wait_us_avg=" << avgWait << "\n"
         << "gpu_wait_us_p50=" << percentile(waits, 0.50) << "\n"
         << "gpu_wait_us_p95=" << percentile(waits, 0.95) << "\n"
         << "gpu_wait_us_p99=" << percentile(waits, 0.99) << "\n"
         << "camera_decode_us_p50=" << percentile(decode, 0.50) << "\n"
         << "camera_decode_us_p95=" << percentile(decode, 0.95) << "\n"
         << "camera_decode_us_p99=" << percentile(decode, 0.99) << "\n"
         << "draw_record_us_p50=" << percentile(draw, 0.50) << "\n"
         << "draw_record_us_p95=" << percentile(draw, 0.95) << "\n"
         << "draw_record_us_p99=" << percentile(draw, 0.99) << "\n"
         << "status=" << acceptance << "\n";

    const fs::path finalPath = state.runDirectory / "summary.txt";
    const fs::path tempPath = state.runDirectory / "summary.txt.tmp";
    if (FaultRequested("summary_open"))
        return false;
    std::ofstream out(tempPath, std::ios::binary);
    if (!out)
        return false;
    if (FaultRequested("summary_write"))
    {
        out.setstate(std::ios::badbit);
        return false;
    }
    const std::string content = text.str();
    const size_t writeSize =
        FaultRequested("summary_partial") ? content.size() / 2 : content.size();
    out.write(content.data(), static_cast<std::streamsize>(writeSize));
    if (FaultRequested("summary_flush"))
    {
        out.flush();
        out.setstate(std::ios::badbit);
    }
    else
    {
        out.flush();
    }
    if (FaultRequested("summary_close"))
    {
        out.close();
        out.setstate(std::ios::badbit);
    }
    else
    {
        out.close();
    }
    if (!out)
        return false;

    std::ifstream in(tempPath, std::ios::binary);
    if (!in)
        return false;
    std::string actual;
    char buffer[4096];
    for (;;)
    {
        in.read(buffer, sizeof(buffer));
        const std::streamsize count = in.gcount();
        if (count > 0)
            actual.append(buffer, static_cast<size_t>(count));
        if (in.bad())
            return false;
        if (in.eof())
            break;
        if (in.fail())
            return false;
    }
    in.close();
    if (actual != content)
        return false;
    if (!ContainsLine(actual, "run_id=" + state.runId) ||
        !ContainsLine(actual,
                      "run_directory=" + PathToUtf8(state.runDirectory)) ||
        !ContainsLine(actual,
                      "run_manifest_sha256=" + state.runManifestSha256))
        return false;
    if (FaultRequested("summary_publish_fail"))
        return false;
    if (FaultRequested("summary_publish_crash"))
        b3a::TerminalExit(92);
    if (!PublishEvidenceFile(tempPath, finalPath))
        return false;
    return true;
}

bool IsRegularNonEmpty(const fs::path& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec &&
           fs::file_size(path, ec) > 0 && !ec;
}

bool ReadAllText(const fs::path& path, std::string& text)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    if (ReadFaultRequested(path, "partial"))
    {
        char partial[16] = {};
        in.read(partial, sizeof(partial));
        text.assign(partial, static_cast<size_t>(in.gcount()));
        return false;
    }
    if (ReadFaultRequested(path, "badbit"))
        return false;
    text.clear();
    char buffer[4096];
    for (;;)
    {
        in.read(buffer, sizeof(buffer));
        const std::streamsize count = in.gcount();
        if (count > 0)
            text.append(buffer, static_cast<size_t>(count));
        if (in.bad())
            return false;
        if (in.eof())
            return true;
        if (in.fail())
            return false;
    }
}

bool ContainsToken(const std::string& text, const std::string& token)
{
    return text.find(token) != std::string::npos;
}

bool ContainsLine(const std::string& text, const std::string& line)
{
    if (text.rfind(line + "\n", 0) == 0)
        return true;
    return text.find("\n" + line + "\n") != std::string::npos;
}

bool ValidatePnmFile(const fs::path& path, char magic, uint32_t expectedWidth,
                     uint32_t expectedHeight, uint64_t bytesPerPixel)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    char actualMagic[2] = {};
    in.read(actualMagic, 2);
    if (in.gcount() != 2 || actualMagic[0] != 'P' || actualMagic[1] != magic)
        return false;
    char newline = 0;
    in.get(newline);
    if (newline != '\n')
        return false;
    std::string dimensions;
    if (!std::getline(in, dimensions) || dimensions.empty() ||
        dimensions.back() == '\r')
        return false;
    const size_t space = dimensions.find(' ');
    if (space == std::string::npos ||
        dimensions.find(' ', space + 1) != std::string::npos)
        return false;
    uint64_t width = 0, height = 0;
    if (!b3b2evidence::Unsigned(dimensions.substr(0, space), width) ||
        !b3b2evidence::Unsigned(dimensions.substr(space + 1), height) ||
        width == 0 || height == 0 || width > 16384 || height > 16384 ||
        width != expectedWidth || height != expectedHeight)
        return false;
    std::string maxval;
    if (!std::getline(in, maxval) || maxval != "255")
        return false;
    const uint64_t pixels = width * height;
    if (pixels > std::numeric_limits<uint64_t>::max() / bytesPerPixel)
        return false;
    const uint64_t payload = pixels * bytesPerPixel;
    const uint64_t header = 2ull + 1ull + dimensions.size() + 1ull +
                            maxval.size() + 1ull;
    if (header > std::numeric_limits<uint64_t>::max() - payload)
        return false;
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec &&
           fs::file_size(path, ec) == header + payload && !ec;
}

bool ValidatePpmFile(const fs::path& path, uint32_t width, uint32_t height)
{
    return ValidatePnmFile(path, '6', width, height, 3);
}

bool ValidatePgmFile(const fs::path& path, uint32_t width, uint32_t height)
{
    return ValidatePnmFile(path, '5', width, height, 1);
}

bool ValidateCsvFile(const fs::path& path, uint64_t expectedFrames,
                     const std::string& runId,
                     b3b2evidence::CsvEvidence& csv)
{
    std::string text;
    return ReadAllText(path, text) &&
           b3b2evidence::CsvValid(text, runId, expectedFrames, csv);
}

bool VerifyEvidenceFiles(PersistentState& state, const fs::path& output,
                         bool auditCloseOk)
{
    auto fail = [&](const char* what) {
        MarkEvidenceFailure(state, what);
        return false;
    };
    if (!auditCloseOk)
        fail("audit_close");
    if (!state.framesClosedOk)
        fail("frames_close");
    if (!state.countersClosedOk)
        fail("counters_close");
    if (state.runId.empty())
        fail("run_id_missing");

    const fs::path manifestPath = output / "run-manifest.txt";
    std::string manifestText;
    b3b2evidence::Fields manifestFields;
    if (!IsRegularNonEmpty(manifestPath))
        fail("run_manifest_missing");
    else if (!ReadAllText(manifestPath, manifestText) ||
             !b3b2evidence::ParseFields(manifestText, manifestFields))
        fail("run_manifest_read");
    else
    {
        std::string manifestRunId, manifestDirectory, manifestMode;
        if (!b3b2evidence::Get(manifestFields, "run_id", manifestRunId) ||
            manifestRunId != state.runId)
            fail("run_manifest_run_id");
        if (!b3b2evidence::Get(manifestFields, "run_directory", manifestDirectory) ||
            manifestDirectory != PathToUtf8(output))
            fail("run_manifest_directory");
        if (b3b2evidence::Get(manifestFields, "mode", manifestMode))
        {
            if (manifestMode != state.mode) fail("run_manifest_mode");
        }
        else if (state.mode != "offline")
            fail("run_manifest_mode");
        uint64_t manifestEvery = 0;
        if (b3b2evidence::GetU64(manifestFields, "sample_every", manifestEvery))
        {
            if (manifestEvery != state.sampleEvery) fail("run_manifest_sample_every");
        }
        else if (state.mode == "live")
            fail("run_manifest_sample_every");
        if (!state.runManifestSha256.empty() &&
            Sha256String(manifestText) != state.runManifestSha256)
            fail("run_manifest_hash");
    }

    // P2: marker presence is not evidence of successful GPU validation.
    struct LogCheck { const char* name; const char* token; };
    const LogCheck logChecks[] = {
        {"gpu-log.txt", "lifetime_guard_active_before_prepare=1"},
        {"gpu-validation.txt", "validation_status="},
        {"geometry-audit.txt", "visual_id="},
        {"infoqueue-dump.txt", "index="},
    };
    for (const LogCheck& check : logChecks)
    {
        std::string text;
        if (!IsRegularNonEmpty(output / check.name) ||
            !ReadAllText(output / check.name, text) ||
            !ContainsToken(text, check.token))
            fail("log_invalid");
    }
    std::string gpuLog, validationLog, infoLog;
    if (!ReadAllText(output / "gpu-log.txt", gpuLog))
        fail("gpu_fence_invalid");

    // Independent per-frame producer evidence.  The sampled manifest and
    // summary/counters are mutable projections; these two logs are written
    // per accepted camera frame and must agree with the GPU fence sequence.
    std::string cameraAuditText, readbackAuditText;
    std::vector<b3b2evidence::CameraAuditRecord> cameraAudit;
    std::vector<b3b2evidence::ReadbackAuditRecord> readbackAudit;
    if (!ReadAllText(output / "camera-audit.txt", cameraAuditText) ||
        !b3b2evidence::ParseCameraAudit(cameraAuditText, cameraAudit) ||
        !ReadAllText(output / "readback-audit.txt", readbackAuditText) ||
        !b3b2evidence::ParseReadbackAudit(readbackAuditText, readbackAudit) ||
        cameraAudit.empty() || readbackAudit.size() != cameraAudit.size())
    {
        // Mandatory per-frame evidence is fail-closed.  Do not continue into
        // the identity loop with an empty/partial audit vector.
        fail("audit_frame_count");
        return false;
    }
    const uint64_t evidenceFrames = state.records.size();
    if (state.mode == "offline" && state.sampleEvery != 1)
        fail("offline_sample_every");
    if (!b3b2evidence::ValidFenceLog(gpuLog, evidenceFrames))
        fail("gpu_fence_invalid");
    b3b2evidence::Fields validation;
    if (!ReadAllText(output / "gpu-validation.txt", validationLog) ||
        !b3b2evidence::ParseFields(validationLog, validation, true) ||
        !b3b2evidence::ValidValidation(validation))
        fail("gpu_validation_invalid");
    b3b2evidence::InfoQueueCounts infoCounts;
    if (!ReadAllText(output / "infoqueue-dump.txt", infoLog) ||
        !b3b2evidence::ValidInfoQueue(infoLog, &infoCounts) ||
        !b3b2evidence::U64Equals(validation, "infoqueue_stored", infoCounts.total) ||
        !b3b2evidence::U64Equals(validation, "infoqueue_enumerated", infoCounts.total) ||
        !b3b2evidence::U64Equals(validation, "infoqueue_warning", infoCounts.warning) ||
        !b3b2evidence::U64Equals(validation, "infoqueue_error", infoCounts.error) ||
        !b3b2evidence::U64Equals(validation, "infoqueue_corruption", infoCounts.corruption) ||
        !b3b2evidence::U64Equals(validation, "infoqueue_info", infoCounts.info) ||
        !b3b2evidence::U64Equals(validation, "infoqueue_message", infoCounts.message))
        fail("infoqueue_invalid");

    // P2/P3: match complete CSV records to both the runtime counters and
    // the independent, strict persistent-counters evidence.
    const fs::path countersPath = output / "persistent-counters.txt";
    std::string countersText;
    b3b2evidence::Fields counters;
    if (!IsRegularNonEmpty(countersPath) ||
        !ReadAllText(countersPath, countersText) ||
        !b3b2evidence::ParseFields(countersText, counters) ||
        !b3b2evidence::U64Equals(counters, "frames_completed", evidenceFrames) ||
        !ContainsLine(countersText, "run_id=" + state.runId))
        fail("counters_invalid");
    std::string countersMode;
    if (b3b2evidence::Get(counters, "mode", countersMode))
    {
        if (countersMode != state.mode) fail("counters_mode");
    }
    else if (state.mode == "live")
        fail("counters_mode");
    uint64_t countersEvery = 0;
    if (b3b2evidence::GetU64(counters, "sample_every", countersEvery))
    {
        if (countersEvery != state.sampleEvery) fail("counters_sample_every");
    }
    else if (state.mode == "live")
        fail("counters_sample_every");

    b3b2evidence::CsvEvidence csv;
    if (!IsRegularNonEmpty(output / "frames.csv") ||
        !ValidateCsvFile(output / "frames.csv", evidenceFrames,
                         state.runId, csv) ||
        csv.rows.size() != evidenceFrames ||
        csv.visible == 0 ||
        (state.sampledVisible != 0 && state.sampledVisible != csv.visible))
        fail("csv_invalid");
    std::set<uint64_t> expectedSamples;
    if (!b3b2evidence::ExpectedSamples(csv, state.mode, state.sampleEvery,
                                       expectedSamples) ||
        csv.sampled != expectedSamples.size() ||
        (state.sampledReadback != 0 &&
         state.sampledReadback != expectedSamples.size()) ||
        !b3b2evidence::ValidCounters(counters, nullptr, evidenceFrames,
                                     expectedSamples.size()) ||
        !b3b2evidence::U64Equals(counters, "frames_sampled",
                                 expectedSamples.size()) ||
        !b3b2evidence::U64Equals(counters, "frames_received",
                                 evidenceFrames - csv.lastFrames))
        fail("csv_invalid");
    state.sampledReadback = expectedSamples.size();
    state.sampledVisible = csv.visible;

    // The CSV row identity must match the independent camera and readback
    // audits, not merely the sampled manifest.  A changed camera hash, fence,
    // session/epoch/sequence or readback tuple is a hard failure.  A live
    // kind=last row is an extra readback of the final live frame, not a new
    // received camera frame, so it maps to the last audit record.
    if (cameraAudit.size() != evidenceFrames - csv.lastFrames ||
        readbackAudit.size() != cameraAudit.size())
    {
        fail("audit_frame_count");
        return false;
    }
    for (const b3b2evidence::CsvRow& row : csv.rows)
    {
        size_t auditIndex = 0;
        bool last = false;
        if (row.kind == "last")
        {
            if (csv.lastFrames != 1 || cameraAudit.empty() ||
                row.frameIndex != cameraAudit.size() + 1)
            {
                fail("audit_identity_mismatch");
                break;
            }
            auditIndex = cameraAudit.size() - 1;
            last = true;
        }
        else
        {
            if (row.frameIndex == 0 || row.frameIndex > cameraAudit.size())
            {
                fail("audit_identity_mismatch");
                break;
            }
            auditIndex = static_cast<size_t>(row.frameIndex - 1);
        }
        if (auditIndex >= cameraAudit.size() || auditIndex >= readbackAudit.size())
        {
            fail("audit_identity_mismatch");
            break;
        }
        b3b2evidence::CameraAuditRecord camera = cameraAudit[auditIndex];
        b3b2evidence::ReadbackAuditRecord readback = readbackAudit[auditIndex];
        if (last)
        {
            camera.frameIndex = row.frameIndex;
            readback.frameIndex = row.frameIndex;
        }
        if (!b3b2evidence::CsvMatchesCameraAudit(row, camera) ||
            !b3b2evidence::CsvMatchesReadbackAudit(row, readback))
        {
            fail("audit_identity_mismatch");
            break;
        }
    }

    // P1: the number of sampled images is reconstructed from the mode and
    // sampling policy, not from the sampled manifest or a mutable counter.
    const fs::path sampledManifestPath =
        output / "sampled" / "evidence-manifest.txt";
    std::string sampledManifestText;
    std::map<uint64_t, b3b2evidence::SampleEntry> entries;
    if (!IsRegularNonEmpty(sampledManifestPath) ||
        !ReadAllText(sampledManifestPath, sampledManifestText) ||
        !b3b2evidence::SampleManifest(sampledManifestText, state.runId,
                                      expectedSamples.size(), entries))
        fail("sampled_manifest_invalid");

    // P1/F6: the sampled directory is a recursive sealed set. Unknown files,
    // nested directories and symlinks are not allowed to survive unnoticed.
    {
        std::set<std::string> allowed;
        allowed.insert("evidence-manifest.txt");
        for (uint64_t index = 1; index <= expectedSamples.size(); ++index)
        {
            const std::string stem = "frame-" + std::to_string(index);
            allowed.insert(stem + "-color.ppm");
            allowed.insert(stem + "-depth.pgm");
            allowed.insert(stem + "-meta.txt");
        }
        std::error_code ec;
        fs::recursive_directory_iterator it(output / "sampled", ec);
        const fs::recursive_directory_iterator end;
        uint64_t sampledFiles = 0;
        while (!ec && it != end)
        {
            if (it->is_symlink(ec) || ec)
            {
                fail("sampled_symlink");
                break;
            }
            if (it->is_directory(ec))
            {
                fail("sampled_nested_directory");
                it.disable_recursion_pending();
                it.increment(ec);
                continue;
            }
            if (!it->is_regular_file(ec) || ec)
            {
                fail("sampled_unknown_entry");
                break;
            }
            const fs::path relative =
                fs::relative(it->path(), output / "sampled", ec);
            if (ec || !allowed.count(PathToUtf8(relative)))
            {
                fail("sampled_unknown_file");
                break;
            }
            ++sampledFiles;
            it.increment(ec);
        }
        if (ec || sampledFiles != expectedSamples.size() * 3 + 1)
            fail("sampled_file_count");
    }

    for (uint64_t index = 1; index <= expectedSamples.size(); ++index)
    {
        const std::string stem = "frame-" + std::to_string(index);
        const fs::path color = output / "sampled" / (stem + "-color.ppm");
        const fs::path depth = output / "sampled" / (stem + "-depth.pgm");
        const fs::path meta = output / "sampled" / (stem + "-meta.txt");
        std::string metaText;
        uint32_t width = 0, height = 0;
        const b3b2evidence::CsvSample* expectedSample =
            index <= csv.samples.size() ?
                &csv.samples[static_cast<size_t>(index - 1)] : nullptr;
        if (!expectedSample || !ReadAllText(meta, metaText) ||
            !b3b2evidence::SampleMetadata(metaText, state.runId, index,
                                          width, height, expectedSample))
        {
            fail("sampled_metadata_invalid");
            continue;
        }
        if (index <= state.sampledDimensions.size() &&
            state.sampledDimensions[static_cast<size_t>(index - 1)] !=
                std::make_pair(width, height))
            fail("sampled_dimension_mismatch");
        else if (state.sampledDimensions.size() < expectedSamples.size())
            state.sampledDimensions.emplace_back(width, height);
        if (!ValidatePpmFile(color, width, height))
            fail("sampled_color_invalid");
        if (!ValidatePgmFile(depth, width, height))
            fail("sampled_depth_invalid");
        std::string colorHash, depthHash, metaHash;
        if (!Sha256File(color, colorHash) || !Sha256File(depth, depthHash) ||
            !Sha256File(meta, metaHash))
        {
            fail("sampled_hash_read");
            continue;
        }
        const auto found = entries.find(index);
        if (found == entries.end() ||
            found->second.color != colorHash ||
            found->second.depth != depthHash ||
            found->second.meta != metaHash)
            fail("sampled_hash_mismatch");
    }
    if (state.sampledReadback == 0 || state.sampledDimensions.size() !=
        static_cast<size_t>(state.sampledReadback))
        fail("sampled_count_invalid");
    return state.evidenceOk;
}

bool CloseFrameCsv(PersistentState& state)
{
    if (state.framesOut.is_open())
    {
        state.framesOut.flush();
        if (FaultRequested("csv_close"))
        {
            state.framesOut.close();
            state.framesOut.setstate(std::ios::badbit);
        }
        else
        {
            state.framesOut.close();
        }
    }
    if (!state.framesOut)
    {
        MarkEvidenceFailure(state, "csv_close");
        state.framesClosedOk = false;
        return false;
    }
    state.framesClosedOk = true;
    return true;
}

bool CloseSampledManifest(PersistentState& state)
{
    if (state.sampledManifestOut.is_open())
    {
        state.sampledManifestOut.flush();
        if (FaultRequested("sampled_manifest_close"))
        {
            state.sampledManifestOut.close();
            state.sampledManifestOut.setstate(std::ios::badbit);
        }
        else
        {
            state.sampledManifestOut.close();
        }
    }
    if (!state.sampledManifestOut)
    {
        MarkEvidenceFailure(state, "sampled_manifest_close");
        state.sampledManifestClosedOk = false;
        return false;
    }
    state.sampledManifestClosedOk = true;
    return true;
}

std::string LineValue(const std::string& text, const std::string& key)
{
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos)
    {
        if (pos == 0 || text[pos - 1] == '\n')
        {
            const size_t start = pos + key.size();
            const size_t end = text.find('\n', start);
            return text.substr(start, end == std::string::npos
                                          ? std::string::npos
                                          : end - start);
        }
        pos += key.size();
    }
    return {};
}

int RunEvidenceVerification(const fs::path& runDirectory)
{
    PersistentState state;
    state.runDirectory = runDirectory;
    state.evidenceOk = true;
    state.framesClosedOk = true;
    state.countersClosedOk = true;
    state.sampledManifestClosedOk = true;

    std::string manifest;
    b3b2evidence::Fields manifestFields;
    if (!ReadAllText(runDirectory / "run-manifest.txt", manifest) ||
        !b3b2evidence::ParseFields(manifest, manifestFields) ||
        !b3b2evidence::Get(manifestFields, "run_id", state.runId) ||
        state.runId.empty())
    {
        std::printf("run_id=\nevidence_status=FAIL\nacceptance_status=UNVERIFIED\n");
        return kEvidenceUnverifiedExitCode;
    }
    state.runManifestSha256 = Sha256String(manifest);

    std::string summary;
    if (!ReadAllText(runDirectory / "summary.txt", summary))
    {
        std::printf("run_id=%s\nevidence_status=FAIL\nacceptance_status=UNVERIFIED\n",
                    state.runId.c_str());
        return kEvidenceUnverifiedExitCode;
    }
    // P1/P2/P3: parse the summary and counters with unique keys and strict
    // decimal values; never use strtoull's permissive partial parsing.
    b3b2evidence::Fields summaryFields, countersFields;
    std::string countersText;
    std::string summaryMode;
    uint64_t framesCompleted = 0, framesSampled = 0, summaryEvery = 0;
    const bool initialOk =
        b3b2evidence::ParseFields(summary, summaryFields) &&
        b3b2evidence::PassSummary(summaryFields) &&
        b3b2evidence::Get(summaryFields, "mode", summaryMode) &&
        (summaryMode == "offline" || summaryMode == "live") &&
        b3b2evidence::GetU64(summaryFields, "frames_completed", framesCompleted) &&
        b3b2evidence::GetU64(summaryFields, "frames_sampled", framesSampled) &&
        framesCompleted > 0 && framesCompleted <= 1000000 &&
        framesSampled > 0 && framesSampled <= framesCompleted &&
        ReadAllText(runDirectory / "persistent-counters.txt", countersText) &&
        b3b2evidence::ParseFields(countersText, countersFields) &&
        b3b2evidence::ValidCounters(countersFields, &summaryFields,
                                    framesCompleted, framesSampled);
    if (!initialOk)
    {
        std::printf("run_id=%s\nevidence_status=FAIL\nacceptance_status=UNVERIFIED\n"
                    "evidence_first_failure=summary_counters\n"
                    "exit=3\n",
                    state.runId.c_str());
        return kEvidenceUnverifiedExitCode;
    }
    state.mode = summaryMode;
    if (b3b2evidence::GetU64(summaryFields, "sample_every", summaryEvery))
        state.sampleEvery = summaryEvery;
    else
        state.sampleEvery = state.mode == "offline" ? 1 : 0;
    if (state.mode == "live" && state.sampleEvery == 0)
        state.sampleEvery = 1;
    state.records.resize(static_cast<size_t>(framesCompleted));
    state.sampledReadback = framesSampled;

    const bool evidence = VerifyEvidenceFiles(state, runDirectory, true);
    const bool summaryIdentity =
        ContainsLine(summary, "run_id=" + state.runId) &&
        ContainsLine(summary,
                     "run_directory=" + PathToUtf8(runDirectory)) &&
        ContainsLine(summary,
                     "run_manifest_sha256=" + state.runManifestSha256);
    const bool summaryMatchesExpectedSamples =
        b3b2evidence::U64Equals(summaryFields, "frames_sampled",
                                state.sampledReadback) &&
        b3b2evidence::U64Equals(summaryFields, "sampled_visible_frames",
                                state.sampledVisible) &&
        b3b2evidence::U64Equals(summaryFields, "frames_completed",
                                state.records.size());
    const bool evidenceAndIdentity = evidence && summaryIdentity &&
        summaryMatchesExpectedSamples;
    const bool summaryClaimsPass = b3b2evidence::PassSummary(summaryFields);
    const bool passed = evidenceAndIdentity && summaryClaimsPass;
    std::printf("run_id=%s\n", state.runId.c_str());
    std::printf("evidence_status=%s\n",
                evidenceAndIdentity ? "PASS" : "FAIL");
    std::printf("evidence_first_failure=%s\n",
                state.evidenceFirstFailure.c_str());
    std::printf("acceptance_status=%s\n", passed ? "PASS" : "UNVERIFIED");
    std::printf("exit=%d\n", passed ? 0 : kEvidenceUnverifiedExitCode);
    return passed ? 0 : kEvidenceUnverifiedExitCode;
}
bool RenderAcceptedCamera(const PersistentOptions& options,
                           uint64_t generation, const char* kind,
                           uint64_t frameIndex, uint64_t decodeUs,
                           const cameracapture12::CameraCaptureFile12& capture,
                           const b3b2::CameraFrameMeta& meta, bool sample,
                           PersistentState& state, Audit& audit)
{
    const gamecamera12::CameraCaptureResult12 result =
        gamecamera12::ValidateCamera12(capture.input);
    if (result.error != gamecamera12::CameraError12::None || !result.camera)
    {
        ++state.counters.validationRejects;
        return false;
    }
    FrameRecord record;
    record.decodeUs = decodeUs;
    if (!RenderPersistentFrame(state, *result.camera, capture, meta, generation,
                               sample, options.sampleEvery, audit, record))
    {
        ++state.counters.framesFailed;
        return false;
    }
    ++state.counters.framesReceived;
    state.records.push_back(record);
    state.distinctProjections.insert(HashMatrix(capture.input.fullTransform));
    state.distinctPositions.insert(HashPosition(capture.input.position));
    if (!WriteFrameCsv(state, record, capture, meta, kind, frameIndex,
                       generation))
    {
        MarkEvidenceFailure(state, "csv");
    }
    return true;
}

bool PublishAndReadOffline(CameraStreamProducer& producer,
                           CameraStreamClient& client,
                           ReceiverValidationState& validation,
                           const PersistentOptions& options,
                           PersistentState& state, Audit& audit,
                           uint64_t generation, const CameraCaptureFile12& capture,
                           uint64_t frameIndex)
{
    CameraPacket packet;
    packet.capture = capture;
    if (!producer.Publish(packet))
    {
        Log(audit.gpu, "offline producer publish rejected");
        return false;
    }
    CameraCaptureFile12 received{};
    b3b2::CameraFrameMeta meta{};
    uint64_t decodeUs = 0;
    bool shutdown = false;
    if (!ReadStreamCamera(client, validation, state, options, audit, generation,
                          received, meta, decodeUs, shutdown))
    {
        Log(audit.gpu, "offline camera read failed frame=%llu",
            static_cast<unsigned long long>(frameIndex));
        return false;
    }
    return RenderAcceptedCamera(options, generation, "offline", frameIndex,
                                decodeUs, received, meta, true, state, audit);
}

bool RunOffline(const PersistentOptions& options,
                const xraylevel12::Geometry& geometry, PersistentState& state,
                Audit& audit)
{
    const std::wstring pipeName = options.pipeName.empty()
                                      ? CameraStreamProducer::MakePipeName()
                                      : options.pipeName;
    CameraStreamProducer producer;
    std::string error;
    if (!producer.Start(pipeName, error))
    {
        Log(audit.gpu, "offline producer Start failed: %s", error.c_str());
        return false;
    }
    producer.OnLevelReady(1, 1);

    CameraStreamClient client;
    if (!client.Connect(pipeName, 5000, error))
    {
        Log(audit.gpu, "offline client Connect failed: %s", error.c_str());
        producer.Stop();
        return false;
    }
    ReceiverValidationState validation;
    validation.BeginConnection(1);

    uint64_t frameIndex = 0;
    for (const fs::path& fixture : options.offlineFixtures)
    {
        std::vector<uint8_t> bytes;
        std::string reason;
        if (!ReadFileBytes(fixture, bytes, reason))
        {
            Log(audit.gpu, "offline fixture read failed: %s", reason.c_str());
            continue;
        }
        CameraCaptureFile12 capture{};
        cameracapture12::FileError fileError = cameracapture12::FileError::None;
        if (!cameracapture12::Decode(bytes, capture, fileError))
        {
            Log(audit.gpu, "offline fixture decode failed: %s",
                cameracapture12::FileErrorMessage(fileError));
            continue;
        }
        if (!LevelNamesEqual(capture.levelName, options.levelName.c_str()))
        {
            Log(audit.gpu, "offline fixture level mismatch: %s", capture.levelName);
            continue;
        }
        capture.input.captureSequence = ++frameIndex;
        const gamecamera12::CameraCaptureResult12 result =
            gamecamera12::ValidateCamera12(capture.input);
        if (result.error != gamecamera12::CameraError12::None)
        {
            Log(audit.gpu, "offline fixture camera invalid: %d",
                static_cast<int>(result.error));
            continue;
        }
        if (!PublishAndReadOffline(producer, client, validation, options, state,
                                   audit, 1, capture, frameIndex))
        {
            producer.Stop();
            client.Close();
            return false;
        }
    }

    cameracapture12::CameraCaptureFile12 synthetic{};
    gamecamera12::CameraCaptureInput12 input{};
    std::optional<gamecamera12::GameCameraSnapshot12> snapshot;
    if (BuildValidationCamera(geometry, 1280, 720, input, snapshot, audit))
    {
        synthetic.input = input;
        std::snprintf(synthetic.levelName, sizeof(synthetic.levelName), "%s",
                      "jupiter");
        synthetic.input.levelSession = 1;
        synthetic.input.captureEpoch = 1;
        synthetic.input.captureSequence = ++frameIndex;
        if (!PublishAndReadOffline(producer, client, validation, options, state,
                                   audit, 1, synthetic, frameIndex))
        {
            producer.Stop();
            client.Close();
            return false;
        }
    }

    producer.Stop();
    client.Close();
    return state.counters.framesCompleted > 0;
}

bool RunLive(const PersistentOptions& options, PersistentState& state,
             Audit& audit)
{
    CameraStreamClient client;
    ReceiverValidationState validation;
    uint64_t connectionGeneration = 0;
    uint64_t accepted = 0;
    const ULONGLONG start = ::GetTickCount64();
    bool producerShutdown = false;
    CameraCaptureFile12 lastCapture{};
    b3b2::CameraFrameMeta lastMeta{};
    bool haveLast = false;
    bool lastFrameSampled = false;
    uint64_t lastDecodeUs = 0;
    uint64_t lastGeneration = 0;

    while (!producerShutdown &&
           (::GetTickCount64() - start) < options.durationMs &&
           (options.maxFrames == 0 || accepted < options.maxFrames))
    {
        if (!client.IsOpen())
        {
            std::string error;
            if (!client.Connect(options.pipeName, 2000, error))
            {
                ::Sleep(100);
                continue;
            }
            ++connectionGeneration;
            validation.BeginConnection(connectionGeneration);
            Log(audit.gpu, "live connected generation=%llu",
                static_cast<unsigned long long>(connectionGeneration));
            continue;
        }

        b3b2::StreamMessage message;
        const b3b2::MessageReadStatus readStatus = client.Read(message, 1000);
        if (readStatus == b3b2::MessageReadStatus::Timeout)
            continue;
        if (readStatus == b3b2::MessageReadStatus::Closed)
        {
            client.Close();
            continue;
        }
        if (readStatus != b3b2::MessageReadStatus::Ok)
        {
            ++state.counters.protocolErrors;
            if (readStatus == b3b2::MessageReadStatus::CrcMismatch)
                ++state.counters.crcErrors;
            client.Close();
            continue;
        }

        if (message.type == b3b2::StreamMessageType::Hello)
        {
            b3b2::HelloPayload hello{};
            if (!b3b2::ParseHello(message.payload, hello))
            {
                ++state.counters.protocolErrors;
                continue;
            }
            std::string reason;
            if (!validation.ObserveHello(hello, reason))
            {
                ++state.counters.validationRejects;
                continue;
            }
            Log(audit.gpu, "live HELLO pid=%u start=%016llX session=%llu epoch=%llu",
                hello.producerPid,
                static_cast<unsigned long long>(hello.producerStartId),
                static_cast<unsigned long long>(hello.levelSession),
                static_cast<unsigned long long>(hello.captureEpoch));
            continue;
        }
        if (message.type == b3b2::StreamMessageType::InvalidateSession)
        {
            b3b2::InvalidatePayload invalidate{};
            if (!b3b2::ParseInvalidate(message.payload, invalidate))
            {
                ++state.counters.protocolErrors;
                continue;
            }
            std::string reason;
            if (!validation.ObserveInvalidate(invalidate, reason))
                ++state.counters.validationRejects;
            if (invalidate.reason == b3b2::InvalidateReason::Shutdown)
                producerShutdown = true;
            continue;
        }
        if (message.type == b3b2::StreamMessageType::Shutdown)
        {
            producerShutdown = true;
            continue;
        }
        if (message.type != b3b2::StreamMessageType::CameraFrame)
            continue;

        std::span<const uint8_t> codec;
        b3b2::CameraFrameMeta meta{};
        if (!b3b2::ParseCameraFrame(message.payload, meta, codec))
        {
            ++state.counters.protocolErrors;
            continue;
        }
        CameraCaptureFile12 capture{};
        cameracapture12::FileError fileError = cameracapture12::FileError::None;
        const uint64_t decodeStart = PersistentQpcNow();
        if (!cameracapture12::Decode(codec, capture, fileError))
        {
            ++state.counters.decodeErrors;
            continue;
        }
        const uint64_t decodeUs = PersistentQpcUs(PersistentQpcNow() - decodeStart);
        if (!LevelNamesEqual(capture.levelName, options.levelName.c_str()))
        {
            ++state.counters.levelRejects;
            ++state.counters.framesDropped;
            continue;
        }
        const gamecamera12::CameraCaptureResult12 validated =
            gamecamera12::ValidateCamera12(capture.input);
        if (validated.error != gamecamera12::CameraError12::None ||
            !validated.camera)
        {
            ++state.counters.validationRejects;
            continue;
        }
        std::string reason;
        if (!validation.ObserveCamera(capture, reason))
        {
            ++state.counters.validationRejects;
            continue;
        }
        ++accepted;
        const b3b2b::SamplePlan plan = b3b2b::PlanSample(
            accepted, options.sampleEvery,
            options.maxFrames != 0 && accepted == options.maxFrames);
        if (!RenderAcceptedCamera(options, connectionGeneration, "live", accepted,
                                  decodeUs, capture, meta, plan.sample, state,
                                  audit))
        {
            client.Close();
            return false;
        }
        lastFrameSampled = plan.sample;
        lastCapture = capture;
        lastMeta = meta;
        lastDecodeUs = decodeUs;
        lastGeneration = connectionGeneration;
        haveLast = true;
    }

    if (haveLast && !lastFrameSampled &&
        b3a::CompletionConfirmed(state.gpu.workState))
    {
        if (!RenderAcceptedCamera(options, lastGeneration, "last", accepted + 1,
                                  lastDecodeUs, lastCapture, lastMeta, true,
                                  state, audit))
        {
            client.Close();
            return false;
        }
        --state.counters.framesReceived;
    }
    client.Close();
    return accepted > 0 && state.counters.framesCompleted > 0;
}

int RunPersistent(const PersistentOptions& options)
{
    RunIdentity run;
    std::string runError;
    if (!CreateRunIdentity(options.output, run, runError))
    {
        std::fprintf(stderr, "run identity failed: %s\n", runError.c_str());
        return 1;
    }

    Audit audit;
    if (!audit.Open(run.directory))
        return 1;
    g_outputForSampling = run.directory;
    std::error_code ec;
    fs::create_directories(run.directory / "sampled", ec);

    xraylevel12::Geometry geometry;
    if (!LoadJupiterGeometry(options, geometry, audit))
        return 1;
    g_geometryForVisibility = geometry;

    PersistentState state;
    state.runId = run.id;
    state.runDirectory = run.directory;
    state.runManifestPath = run.manifest;
    state.mode = options.offline ? "offline" : "live";
    state.sampleEvery = options.offline ? 1 :
        (options.sampleEvery == 0 ? 1 : options.sampleEvery);

    // P1: the terminal guard must be alive before the geometry upload, which
    // is the first possible ExecuteCommandLists. Audit and state outlive it.
    auto terminalAction = [&]() noexcept {
        audit.Flush();
        b3a::TerminalExit(b3a::kTerminalGpuExitCode);
    };
    b3a::UnknownCompletionGuard guard(state.gpu.workState, terminalAction);
    Log(audit.gpu, "lifetime_guard_active_before_prepare=1");
    Log(audit.gpu, "run_id=%s", state.runId.c_str());

    if (!WriteRunManifest(run, state.mode, state.sampleEvery,
                          state.runManifestSha256, runError))
    {
        std::fprintf(stderr, "run manifest failed: %s\n", runError.c_str());
        return 1;
    }
    std::printf("run_id=%s run_directory=%s\n", state.runId.c_str(),
                PathToUtf8(state.runDirectory).c_str());
    Log(audit.gpu, "run_manifest_sha256=%s", state.runManifestSha256.c_str());

    if (FaultRequested("csv_open"))
    {
        MarkEvidenceFailure(state, "csv_open");
    }
    else
    {
        state.framesOut.open(run.directory / "frames.csv", std::ios::binary);
        if (!state.framesOut)
            MarkEvidenceFailure(state, "csv_open");
        else
        {
            state.framesOut
                << "run_id,frame_index,kind,connection_generation,level_session,"
                   "capture_epoch,capture_sequence,source_game_frame,"
                   "camera_hash,camera_sha256,viewport,position,"
                   "frame_received_qpc,gpu_submit_qpc,gpu_completion_qpc,"
                   "frame_age_ms,camera_decode_us,draw_record_us,gpu_wait_us,"
                   "fence_value,sample,coverage,non_clear_pixels,bbox,"
                   "depth_min,depth_max,status\n";
            if (!state.framesOut)
                MarkEvidenceFailure(state, "csv_header");
        }
    }
    if (FaultRequested("counters_open"))
    {
        MarkEvidenceFailure(state, "counters_open");
    }
    else
    {
        state.countersOut.open(run.directory / "persistent-counters.txt",
                               std::ios::binary);
        if (!state.countersOut)
            MarkEvidenceFailure(state, "counters_open");
    }
    if (FaultRequested("sampled_manifest_open"))
    {
        MarkEvidenceFailure(state, "sampled_manifest_open");
    }
    else
    {
        state.sampledManifestOut.open(
            run.directory / "sampled" / "evidence-manifest.txt",
            std::ios::binary);
        if (!state.sampledManifestOut)
            MarkEvidenceFailure(state, "sampled_manifest_open");
        else
        {
            state.sampledManifestOut << "run_id=" << state.runId << "\n";
            if (!state.sampledManifestOut)
                MarkEvidenceFailure(state, "sampled_manifest_open");
        }
    }
    if (!PreparePersistentRenderer(state, geometry, audit))
        return 1;

    bool ok = false;
    std::string validationStatus = "UNVERIFIED";
    bool validationPassed = false;
    if (options.offline)
        ok = RunOffline(options, geometry, state, audit);
    else
        ok = RunLive(options, state, audit);

    if (FaultRequested("crash_after_frames"))
        b3a::TerminalExit(91);

    if (b3a::CompletionConfirmed(state.gpu.workState) && state.gpu.device)
    {
        const InfoQueueResult info = InspectInfoQueue(
            state.gpu.device.Get(), audit, true, g_debugLayerAvailable,
            g_gbvRequested, g_deniedAtStart);
        validationStatus = info.status;
        validationPassed = b3a::ValidationPasses(info.status);
    }

    WriteCounters(state);
    CloseFrameCsv(state);
    CloseSampledManifest(state);
    const bool auditCloseOk = audit.CloseAndVerify();
    if (FaultRequested("manifest_mismatch"))
    {
        std::ofstream overwrite(run.manifest, std::ios::binary);
        overwrite << "run_id=manifest-mismatch\n";
    }
    if (FaultRequested("stale_csv"))
    {
        const char* stale = std::getenv("B3B2B_STALE_CSV");
        if (stale != nullptr)
        {
            std::error_code copyEc;
            fs::copy_file(fs::path(stale), run.directory / "frames.csv",
                          fs::copy_options::overwrite_existing, copyEc);
        }
    }
    if (FaultRequested("stale_image"))
    {
        const char* stale = std::getenv("B3B2B_STALE_IMAGE");
        if (stale != nullptr)
        {
            std::error_code copyEc;
            fs::copy_file(fs::path(stale), run.directory / "sampled" /
                                                 "frame-1-color.ppm",
                          fs::copy_options::overwrite_existing, copyEc);
        }
    }
    const bool evidencePassed =
        VerifyEvidenceFiles(state, run.directory, auditCloseOk);
    if (FaultRequested("crash_after_evidence"))
        b3a::TerminalExit(92);
    const bool gpuOk = ok && state.counters.framesCompleted > 0 &&
                       state.renderFailures == 0 &&
                       b3a::CompletionConfirmed(state.gpu.workState);
    const bool visibilityOk = state.sampledVisible > 0;
    const char* acceptance = b3b2b::EvaluateAcceptance(
        gpuOk, validationStatus.c_str(), evidencePassed, visibilityOk);
    const int exitCode = b3b2b::AcceptanceExitCode(acceptance);
    if (!WritePersistentSummary(state, run.directory, acceptance,
                                gpuOk ? "PASS" : "FAIL",
                                options.offline ? "offline" : "live",
                                validationStatus, validationPassed,
                                evidencePassed, visibilityOk))
    {
        std::fprintf(stderr,
                     "summary evidence could not be published; "
                     "run_id=%s acceptance was %s\n",
                     state.runId.c_str(), acceptance);
        return kEvidenceUnverifiedExitCode;
    }
    std::printf("acceptance_status=%s run_id=%s\n", acceptance,
                state.runId.c_str());
    return exitCode;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
    {
        const std::string value = Utf8FromWide(argv[i]);
        if (!value.empty() || argv[i][0] == L'\0')
            args.push_back(value);
        else
        {
            std::fprintf(stderr, "invalid UTF-16 argument\n");
            return 2;
        }
    }
    if (args.size() >= 3 && args[1] == "--verify-evidence")
    {
        const fs::path requested = fs::path(argv[2]);
        std::error_code ec;
        const fs::path absolute = fs::absolute(requested, ec);
        try
        {
            return RunEvidenceVerification(ec ? requested : absolute);
        }
        catch (...)
        {
            std::printf("evidence_status=FAIL\nacceptance_status=UNVERIFIED\nexit=3\n");
            return kEvidenceUnverifiedExitCode;
        }
    }
    PersistentOptions options;
    std::string reason;
    if (!ParsePersistentOptions(args, options, reason))
    {
        std::fprintf(stderr, "%s\n", reason.c_str());
        return 2;
    }
    try
    {
        return RunPersistent(options);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "unhandled exception: %s\n", error.what());
        return 20;
    }
    catch (...)
    {
        std::fprintf(stderr, "unhandled unknown exception\n");
        return 21;
    }
}
