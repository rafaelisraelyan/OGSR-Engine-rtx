// F4.0-B3-A: standalone D3D12 offscreen proof for real B1 X-Ray geometry.
//
// The game executable, CHW12, DX11/R4 and the B1 reader are not modified by
// this test. The test creates its own D3D12 device, command queue and fence,
// uploads the compact B1 visual to default-heap VB/IB buffers, renders to an
// independent color/depth pair, and reads the color/depth data back after a
// bounded GPU fence wait.

#include "b3a_gpu_state.h"
#include "dx12gamecamera.h"
#include "dx12xraylevelgeometry.h"
#include "dx12fencewait.h"
#include "dx12lifetime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace
{
constexpr uint32_t kDefaultWidth = 1280;
constexpr uint32_t kDefaultHeight = 720;
constexpr uint32_t kFenceTimeoutMs = 10000;

struct Options
{
    fs::path level;
    fs::path geom;
    fs::path output;
    uint32_t visual = 3;
    uint32_t width = kDefaultWidth;
    uint32_t height = kDefaultHeight;
};

struct Audit
{
    std::ofstream gpu;
    std::ofstream geometry;
    std::ofstream camera;
    std::ofstream readback;
    std::ofstream validation;
    std::ofstream infoQueue;

    bool Open(const fs::path& output)
    {
        std::error_code ec;
        fs::create_directories(output, ec);
        if (ec)
            return false;
        gpu.open(output / "gpu-log.txt", std::ios::binary);
        geometry.open(output / "geometry-audit.txt", std::ios::binary);
        camera.open(output / "camera-audit.txt", std::ios::binary);
        readback.open(output / "readback-audit.txt", std::ios::binary);
        validation.open(output / "gpu-validation.txt", std::ios::binary);
        infoQueue.open(output / "infoqueue-dump.txt", std::ios::binary);
        return gpu && geometry && camera && readback && validation && infoQueue;
    }

    // Best-effort flush for the terminal path: std::_Exit does not flush
    // stdio or iostream buffers.
    void Flush() noexcept
    {
        std::ofstream* streams[] = {&gpu, &geometry, &camera,
                                    &readback, &validation, &infoQueue};
        for (std::ofstream* stream : streams)
        {
            try
            {
                stream->flush();
            }
            catch (...)
            {
            }
        }
    }
};

void Log(std::ofstream& out, const char* text)
{
    if (out)
        out << text << "\n";
}

template <typename... T>
void Log(std::ofstream& out, const char* fmt, T... values)
{
    char buffer[2048];
    std::snprintf(buffer, sizeof(buffer), fmt, values...);
    if (out)
        out << buffer << "\n";
}

bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>& bytes, std::string& reason)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        reason = "cannot open " + path.string();
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) >
                         static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        reason = "invalid file size " + path.string();
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file)
    {
        reason = "cannot read " + path.string();
        return false;
    }
    return true;
}

bool WritePpm(const fs::path& path, uint32_t width, uint32_t height,
              const std::vector<uint8_t>& rgba, uint32_t rowPitch)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    file << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* row = rgba.data() + static_cast<size_t>(y) * rowPitch;
        for (uint32_t x = 0; x < width; ++x)
        {
            file.put(static_cast<char>(row[x * 4 + 0]));
            file.put(static_cast<char>(row[x * 4 + 1]));
            file.put(static_cast<char>(row[x * 4 + 2]));
        }
    }
    return static_cast<bool>(file);
}

bool WriteDepthPgm(const fs::path& path, uint32_t width, uint32_t height,
                   const std::vector<float>& depth, uint32_t rowPitch)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    file << "P5\n" << width << " " << height << "\n255\n";
    const uint32_t rowFloats = rowPitch / sizeof(float);
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            float value = 1.0f;
            std::memcpy(&value,
                        depth.data() + static_cast<size_t>(y) * rowFloats + x,
                        sizeof(value));
            const float normalized = std::clamp(value, 0.0f, 1.0f);
            file.put(static_cast<char>(static_cast<uint8_t>(normalized * 255.0f)));
        }
    }
    return static_cast<bool>(file);
}

std::string Narrow(const wchar_t* text)
{
    if (!text)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr,
                        nullptr);
    return result;
}

void LogAdapterInfo(Audit& audit)
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
    {
        Log(audit.validation, "adapter_info=unavailable");
        return;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(0, adapter.GetAddressOf())))
    {
        Log(audit.validation, "adapter_info=unavailable");
        return;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)))
    {
        Log(audit.validation, "adapter_info=unavailable");
        return;
    }
    Log(audit.validation, "adapter_name=%s", Narrow(desc.Description).c_str());
    Log(audit.validation, "adapter_vendor_id=0x%04X",
        static_cast<unsigned>(desc.VendorId));
    Log(audit.validation, "adapter_device_id=0x%04X",
        static_cast<unsigned>(desc.DeviceId));
    Log(audit.validation, "adapter_is_software=%d",
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? 1 : 0);
}

bool ParseOptions(int argc, char** argv, Options& options, std::string& reason)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) -> bool {
            if (i + 1 >= argc)
                return false;
            value = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--level")
        {
            if (!next(value))
            {
                reason = "missing value for --level";
                return false;
            }
            options.level = value;
        }
        else if (arg == "--geom")
        {
            if (!next(value))
            {
                reason = "missing value for --geom";
                return false;
            }
            options.geom = value;
        }
        else if (arg == "--output")
        {
            if (!next(value))
            {
                reason = "missing value for --output";
                return false;
            }
            options.output = value;
        }
        else if (arg == "--visual")
        {
            if (!next(value))
            {
                reason = "missing value for --visual";
                return false;
            }
            options.visual = static_cast<uint32_t>(std::stoul(value));
        }
        else if (arg == "--width" || arg == "--height")
        {
            if (!next(value))
            {
                reason = "missing value for " + arg;
                return false;
            }
            const uint32_t parsed = static_cast<uint32_t>(std::stoul(value));
            if (arg == "--width")
                options.width = parsed;
            else
                options.height = parsed;
        }
        else
        {
            reason = "unknown argument " + arg;
            return false;
        }
    }
    if (options.level.empty() || options.geom.empty() || options.output.empty())
    {
        reason = "MISSING REAL LEVEL ASSETS: --level, --geom and --output are required";
        return false;
    }
    if (options.width == 0 || options.height == 0)
    {
        reason = "invalid offscreen dimensions";
        return false;
    }
    return true;
}

struct PositionElement
{
    bool found = false;
    uint32_t offset = 0;
    uint32_t stride = 0;
};

PositionElement ValidateDeclaration(const xraylevel12::Geometry& geometry,
                                    std::string& reason)
{
    PositionElement result;
    result.stride = geometry.vertex_stride;
    for (uint32_t i = 0; i < geometry.declaration_count; ++i)
    {
        const auto& element = geometry.declaration[i];
        if (element.type == 17 || element.stream == 255)
            continue; // D3DDECLTYPE_UNUSED terminator
        if (element.usage == 0) // D3DDECLUSAGE_POSITION
        {
            if (element.usage_index != 0 || element.stream != 0 ||
                element.method != 0 || element.type != 2 /* D3DDECLTYPE_FLOAT3 */)
            {
                reason = "unsupported POSITION declaration";
                return {};
            }
            if (result.found)
            {
                reason = "duplicate POSITION declaration";
                return {};
            }
            result.found = true;
            result.offset = element.offset;
        }
    }
    if (!result.found)
        reason = "missing POSITION declaration";
    return result;
}

bool ReadPosition(const xraylevel12::Geometry& geometry, uint32_t index,
                  DirectX::XMFLOAT3& position, std::string& reason)
{
    if (index >= geometry.vertex_count)
    {
        reason = "vertex index outside compact range";
        return false;
    }
    if (geometry.vertex_stride < 3 * sizeof(float))
    {
        reason = "vertex stride cannot contain POSITION";
        return false;
    }
    const uint64_t offset =
        static_cast<uint64_t>(index) * geometry.vertex_stride;
    if (offset + 3 * sizeof(float) > geometry.vertex_bytes.size())
    {
        reason = "POSITION read exceeds compact vertex bytes";
        return false;
    }
    float raw[3];
    std::memcpy(raw, geometry.vertex_bytes.data() + offset, sizeof(raw));
    if (!std::isfinite(raw[0]) || !std::isfinite(raw[1]) ||
        !std::isfinite(raw[2]))
    {
        reason = "non-finite POSITION";
        return false;
    }
    position = {raw[0], raw[1], raw[2]};
    return true;
}
bool ValidateGeometry(const xraylevel12::Geometry& geometry, std::string& reason)
{
    if (geometry.vertex_stride != 32 || geometry.vertex_count != 120 ||
        geometry.index_count != 192 || geometry.triangle_count != 64 ||
        geometry.first_index != 0 || geometry.base_vertex != 0 ||
        geometry.vertex_bytes.size() != 120ull * 32ull ||
        geometry.indices.size() != 192)
    {
        reason = "unexpected compact geometry shape for Jupiter visual 3";
        return false;
    }
    const PositionElement declaration = ValidateDeclaration(geometry, reason);
    if (!declaration.found || declaration.offset != 0)
        return false;
    if (geometry.bounds.minimum[0] > geometry.bounds.maximum[0] ||
        geometry.bounds.minimum[1] > geometry.bounds.maximum[1] ||
        geometry.bounds.minimum[2] > geometry.bounds.maximum[2])
    {
        reason = "invalid geometry bounds";
        return false;
    }
    uint32_t maxIndex = 0;
    for (uint16_t index : geometry.indices)
        maxIndex = std::max(maxIndex, static_cast<uint32_t>(index));
    if (maxIndex >= geometry.vertex_count)
    {
        reason = "index outside compact vertex range";
        return false;
    }
    for (uint32_t i = 0; i < geometry.vertex_count; ++i)
    {
        DirectX::XMFLOAT3 position{};
        if (!ReadPosition(geometry, i, position, reason))
            return false;
    }
    return true;
}

using b3a::UniqueHandle;
using b3a::GpuWorkState;
using b3a::ListState;
using b3a::FenceOutcome;
using b3a::MaySubmit;
using b3a::MayResetList;
using b3a::MustRetainUntilCompletion;
using b3a::StateAfterExecute;
using b3a::StateAfterFenceWait;
using b3a::ListStateAfterClose;
using b3a::ListStateAfterFailedClose;
using b3a::ListStateAfterReset;
using b3a::ListStateAfterConfirmedSubmit;

struct GpuOwner
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    UniqueHandle fenceEvent;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> color;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> constants;
    ComPtr<ID3D12Resource> clearReadback;
    ComPtr<ID3D12Resource> colorReadback;
    ComPtr<ID3D12Resource> depthReadback;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;

    GpuWorkState workState = GpuWorkState::NoWorkSubmitted;
    ListState listState = ListState::Recording;

    GpuOwner() = default;
    GpuOwner(const GpuOwner&) = delete;
    GpuOwner& operator=(const GpuOwner&) = delete;
    GpuOwner(GpuOwner&&) noexcept = default;
    GpuOwner& operator=(GpuOwner&&) noexcept = default;
    ~GpuOwner() = default;
};

// Terminal failure context for the standalone process. The process must not
// release GPU-backed objects whose completion is unconfirmed, so the failure
// path records the state, flushes diagnostics and calls std::_Exit: stack
// unwinding, static destruction and atexit handlers are skipped, therefore the
// live GpuOwner is never destroyed by C++.
struct GpuFailureContext
{
    const char* stage = "unknown";
    uint64_t fenceRequested = 0;
    uint64_t fenceCompleted = 0;
    long deviceRemovedReason = 0;
    long lastResult = 0;
};

const char* ToString(GpuWorkState state) noexcept
{
    switch (state)
    {
    case GpuWorkState::NoWorkSubmitted:
        return "NoWorkSubmitted";
    case GpuWorkState::WorkSubmittedUnknown:
        return "WorkSubmittedUnknown";
    case GpuWorkState::WorkCompleted:
        return "WorkCompleted";
    case GpuWorkState::DeviceRemoved:
        return "DeviceRemoved";
    }
    return "UnknownState";
}

const char* ToString(ListState state) noexcept
{
    switch (state)
    {
    case ListState::Recording:
        return "Recording";
    case ListState::Closed:
        return "Closed";
    case ListState::CloseFailed:
        return "CloseFailed";
    case ListState::ResetReady:
        return "ResetReady";
    }
    return "UnknownState";
}

[[noreturn]] void TerminalGpuFailure(Audit& audit, const GpuOwner& owner,
                                     int exitCode,
                                     const GpuFailureContext& context) noexcept
{
    // Best effort only: the terminal decision itself needs no allocation, and
    // nothing may escape from here.
    try
    {
        GpuFailureContext snapshot = context;
        if (owner.fence)
            snapshot.fenceCompleted = owner.fence->GetCompletedValue();
        if (owner.device && snapshot.deviceRemovedReason == 0)
            snapshot.deviceRemovedReason =
                static_cast<long>(owner.device->GetDeviceRemovedReason());

        Log(audit.gpu, "terminal_gpu_state=%s", ToString(owner.workState));
        Log(audit.gpu, "terminal_list_state=%s", ToString(owner.listState));
        Log(audit.gpu, "terminal_stage=%s",
            snapshot.stage ? snapshot.stage : "unknown");
        Log(audit.gpu, "terminal_fence_requested=%llu",
            static_cast<unsigned long long>(snapshot.fenceRequested));
        Log(audit.gpu, "terminal_fence_completed=%llu",
            static_cast<unsigned long long>(snapshot.fenceCompleted));
        Log(audit.gpu, "terminal_device_removed_reason=0x%08X",
            static_cast<unsigned>(snapshot.deviceRemovedReason));
        Log(audit.gpu, "terminal_last_result=0x%08X",
            static_cast<unsigned>(snapshot.lastResult));
        Log(audit.gpu, "terminal_completion_confirmed=0");
        Log(audit.gpu, "terminal_exit=%d", exitCode);
        Log(audit.gpu, "frame_status=TERMINAL_GPU_FAILURE");

        Log(audit.validation, "validation_status=UNVERIFIED");
        Log(audit.validation, "terminal_gpu_state=%s", ToString(owner.workState));
        Log(audit.validation, "terminal_stage=%s",
            snapshot.stage ? snapshot.stage : "unknown");
        audit.Flush();
    }
    catch (...)
    {
    }
    std::fflush(stderr);
    b3a::TerminalExit(exitCode);
}

struct FenceWait
{
    ID3D12Fence* fence = nullptr;
    HANDLE event = nullptr;

    uint64_t GetCompletedValue() const noexcept { return fence->GetCompletedValue(); }

    bool SetEventOnCompletion(uint64_t value) noexcept
    {
        return SUCCEEDED(fence->SetEventOnCompletion(value, event));
    }

    dx12fence12::WaitStatus Wait(uint32_t timeoutMs) noexcept
    {
        const DWORD result = WaitForSingleObject(event, timeoutMs);
        if (result == WAIT_OBJECT_0)
            return dx12fence12::WaitStatus::Signaled;
        if (result == WAIT_TIMEOUT)
            return dx12fence12::WaitStatus::Timeout;
        return dx12fence12::WaitStatus::Failed;
    }
};

bool Check(HRESULT result, const char* operation, Audit& audit)
{
    if (SUCCEEDED(result))
        return true;
    Log(audit.gpu, "%s failed HRESULT=0x%08X", operation,
        static_cast<unsigned>(result));
    return false;
}

struct InfoQueueResult
{
    bool queueAvailable = false;
    bool complete = false;
    uint64_t stored = 0;
    uint64_t enumerated = 0;
    uint64_t retrievalFailures = 0;
    uint64_t errors = 0;
    uint64_t corruptions = 0;
    uint64_t warnings = 0;
    uint64_t info = 0;
    uint64_t messages = 0;
    uint64_t deniedByFilter = 0;
    uint64_t hiddenByFilter = 0;
    uint64_t discarded = 0;
    bool debugLayerAvailable = false;
    bool gbvRequested = false;
    bool gbvActive = false;
    bool completionConfirmed = false;
    const char* status = "UNVERIFIED";
};

InfoQueueResult InspectInfoQueue(ID3D12Device* device, Audit& audit,
                                 bool completionConfirmed,
                                 bool debugLayerAvailable,
                                 bool gbvRequested,
                                 uint64_t deniedAtStart)
{
    InfoQueueResult result;
    result.completionConfirmed = completionConfirmed;
    result.debugLayerAvailable = debugLayerAvailable;
    result.gbvRequested = gbvRequested;

    ComPtr<ID3D12InfoQueue> queue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf()))))
    {
        Log(audit.validation, "infoqueue_available=0");
        Log(audit.validation, "validation_status=UNVERIFIED");
        result.status = "UNVERIFIED";
        return result;
    }
    result.queueAvailable = true;
    result.stored = queue->GetNumStoredMessages();
    result.deniedByFilter = queue->GetNumMessagesDeniedByStorageFilter();
    result.discarded = queue->GetNumMessagesDiscardedByMessageCountLimit();
    result.hiddenByFilter =
        result.deniedByFilter > deniedAtStart ? result.deniedByFilter - deniedAtStart : 0;

    for (UINT64 i = 0; i < result.stored; ++i)
    {
        SIZE_T size = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &size)) || size == 0)
        {
            ++result.retrievalFailures;
            continue;
        }
        // D3D12_MESSAGE has pointer/fixed members and must be aligned.
        std::vector<uint8_t> storage(size + alignof(D3D12_MESSAGE) - 1);
        void* aligned = storage.data();
        size_t space = storage.size();
        if (!std::align(alignof(D3D12_MESSAGE), size, aligned, space))
        {
            ++result.retrievalFailures;
            continue;
        }
        auto* message = static_cast<D3D12_MESSAGE*>(aligned);
        SIZE_T actual = size;
        if (FAILED(queue->GetMessage(i, message, &actual)))
        {
            ++result.retrievalFailures;
            continue;
        }
        ++result.enumerated;
        // GPU-Based Validation active evidence: the debug layer emits this
        // message on device creation once GBV is actually enabled. This SDK's
        // ID3D12Debug3 exposes no getter, so the message is the evidence.
        if (static_cast<unsigned>(message->ID) == 1016u &&
            message->pDescription != nullptr &&
            std::strstr(message->pDescription, "GPU-Based Validation") != nullptr)
        {
            result.gbvActive = true;
            Log(audit.validation, "gbv_active_evidence=ID1016");
        }
        switch (message->Severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            ++result.corruptions;
            break;
        case D3D12_MESSAGE_SEVERITY_ERROR:
            ++result.errors;
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            ++result.warnings;
            break;
        case D3D12_MESSAGE_SEVERITY_INFO:
            ++result.info;
            break;
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
            ++result.messages;
            break;
        }
        Log(audit.infoQueue,
            "index=%llu severity=%d id=%d description_length=%llu description=%s",
            static_cast<unsigned long long>(i), static_cast<int>(message->Severity),
            static_cast<int>(message->ID),
            static_cast<unsigned long long>(message->DescriptionByteLength),
            message->pDescription ? message->pDescription : "");
    }
    result.complete = (result.enumerated == result.stored) &&
                      (result.retrievalFailures == 0);

    Log(audit.validation, "infoqueue_available=%d", result.queueAvailable ? 1 : 0);
    Log(audit.validation, "infoqueue_stored=%llu",
        static_cast<unsigned long long>(result.stored));
    Log(audit.validation, "infoqueue_enumerated=%llu",
        static_cast<unsigned long long>(result.enumerated));
    Log(audit.validation, "infoqueue_retrieval_failures=%llu",
        static_cast<unsigned long long>(result.retrievalFailures));
    Log(audit.validation, "infoqueue_error=%llu",
        static_cast<unsigned long long>(result.errors));
    Log(audit.validation, "infoqueue_corruption=%llu",
        static_cast<unsigned long long>(result.corruptions));
    Log(audit.validation, "infoqueue_warning=%llu",
        static_cast<unsigned long long>(result.warnings));
    Log(audit.validation, "infoqueue_info=%llu",
        static_cast<unsigned long long>(result.info));
    Log(audit.validation, "infoqueue_message=%llu",
        static_cast<unsigned long long>(result.messages));
    Log(audit.validation, "infoqueue_denied_by_filter=%llu",
        static_cast<unsigned long long>(result.deniedByFilter));
    Log(audit.validation, "infoqueue_hidden_after_baseline=%llu",
        static_cast<unsigned long long>(result.hiddenByFilter));
    Log(audit.validation, "infoqueue_discarded=%llu",
        static_cast<unsigned long long>(result.discarded));
    Log(audit.validation, "debug_layer_available=%d",
        result.debugLayerAvailable ? 1 : 0);
    Log(audit.validation, "gbv_requested=%d", result.gbvRequested ? 1 : 0);
    Log(audit.validation, "gbv_active=%d", result.gbvActive ? 1 : 0);
    Log(audit.validation, "completion_confirmed=%d",
        result.completionConfirmed ? 1 : 0);

    b3a::ValidationEvidence evidence;
    evidence.debugLayerAvailable = result.debugLayerAvailable;
    evidence.queueAvailable = result.queueAvailable;
    evidence.enumerationComplete = result.complete;
    evidence.completionConfirmed = result.completionConfirmed;
    evidence.gbvActive = result.gbvActive;
    evidence.errors = result.errors;
    evidence.corruptions = result.corruptions;
    evidence.warnings = result.warnings;
    evidence.discarded = result.discarded;
    evidence.hiddenByFilter = result.hiddenByFilter;
    result.status = b3a::EvaluateValidation(evidence);
    Log(audit.validation, "validation_status=%s", result.status);
    Log(audit.gpu, "infoqueue_status=%s errors=%llu corruption=%llu",
        result.status, static_cast<unsigned long long>(result.errors),
        static_cast<unsigned long long>(result.corruptions));
    return result;
}
FenceOutcome WaitForFence(ID3D12Fence* fence, HANDLE event, uint64_t value,
                         Audit& audit)
{
    FenceWait wait{fence, event};
    const auto result = dx12fence12::WaitForValue(wait, value, kFenceTimeoutMs);
    Log(audit.gpu, "fence requested=%llu completed=%llu outcome=%s",
        static_cast<unsigned long long>(value),
        static_cast<unsigned long long>(result.completedAfter),
        dx12fence12::ToString(result.outcome));
    switch (result.outcome)
    {
    case dx12fence12::Outcome::Completed:
        return FenceOutcome::Completed;
    case dx12fence12::Outcome::Timeout:
        return FenceOutcome::Timeout;
    default:
        return FenceOutcome::Failed;
    }
}

D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    return heap;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t size)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

D3D12_RESOURCE_DESC TextureDesc(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM)
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    else if (format == DXGI_FORMAT_D32_FLOAT)
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    return desc;
}

bool CreateCommitted(ID3D12Device* device, const D3D12_HEAP_PROPERTIES& heap,
                     const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state,
                     const D3D12_CLEAR_VALUE* clear, ID3D12Resource** resource,
                     Audit& audit, const char* name)
{
    const HRESULT result = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear,
        IID_PPV_ARGS(resource));
    if (FAILED(result))
    {
        Log(audit.gpu, "CreateCommittedResource(%s) failed 0x%08X", name,
            static_cast<unsigned>(result));
        return false;
    }
    return true;
}

bool CopyBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                ID3D12Resource* destination, const void* source, uint64_t size,
                D3D12_RESOURCE_STATES destinationState, Audit& audit,
                const char* name,
                std::vector<ComPtr<ID3D12Resource>>& retainedUploads)
{
    ComPtr<ID3D12Resource> upload;
    if (!CreateCommitted(device, Heap(D3D12_HEAP_TYPE_UPLOAD), BufferDesc(size),
                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                         upload.GetAddressOf(), audit, name))
        return false;
    retainedUploads.push_back(upload);

    void* mapped = nullptr;
    if (!Check(upload->Map(0, nullptr, &mapped), "Map upload", audit))
        return false;
    std::memcpy(mapped, source, static_cast<size_t>(size));
    upload->Unmap(0, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = destination;
    barrier.Transition.Subresource = 0; // buffers do not accept ALL_SUBRESOURCES
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(1, &barrier);
    list->CopyBufferRegion(destination, 0, upload.Get(), 0, size);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = destinationState;
    list->ResourceBarrier(1, &barrier);
    return true;
}

bool CreateRootSignature(ID3D12Device* device, ID3D12RootSignature** signature,
                         Audit& audit)
{
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT result = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), error.GetAddressOf());
    if (FAILED(result))
    {
        Log(audit.gpu, "D3D12SerializeRootSignature failed 0x%08X", static_cast<unsigned>(result));
        return false;
    }
    result = device->CreateRootSignature(0, blob->GetBufferPointer(),
                                         blob->GetBufferSize(),
                                         IID_PPV_ARGS(signature));
    if (FAILED(result))
    {
        Log(audit.gpu, "CreateRootSignature failed 0x%08X", static_cast<unsigned>(result));
        return false;
    }
    return true;
}

bool CreatePipeline(ID3D12Device* device, ID3D12RootSignature* root,
                    ID3D12PipelineState** pso, Audit& audit)
{
    static const char* shaderSource = R"(
        cbuffer Camera : register(b0)
        {
            row_major float4x4 gViewProjection;
        };

        struct VSInput
        {
            float3 position : POSITION;
        };

        float4 VSMain(VSInput input) : SV_Position
        {
            return mul(float4(input.position, 1.0), gViewProjection);
        }

        float4 PSMain(float4 position : SV_Position) : SV_Target
        {
            return float4(0.10, 0.85, 0.25, 1.0);
        }
    )";

    ComPtr<ID3DBlob> vertex;
    ComPtr<ID3DBlob> pixel;
    ComPtr<ID3DBlob> error;
    HRESULT result = D3DCompile(shaderSource, std::strlen(shaderSource), "b3a_shader.hlsl",
                                nullptr, nullptr, "VSMain", "vs_5_0",
                                D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                vertex.GetAddressOf(), error.GetAddressOf());
    if (FAILED(result))
    {
        Log(audit.gpu, "vertex shader compilation failed 0x%08X", static_cast<unsigned>(result));
        return false;
    }
    error.Reset();
    result = D3DCompile(shaderSource, std::strlen(shaderSource), "b3a_shader.hlsl",
                        nullptr, nullptr, "PSMain", "ps_5_0",
                        D3DCOMPILE_ENABLE_STRICTNESS, 0,
                        pixel.GetAddressOf(), error.GetAddressOf());
    if (FAILED(result))
    {
        Log(audit.gpu, "pixel shader compilation failed 0x%08X", static_cast<unsigned>(result));
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC inputElement{};
    inputElement.SemanticName = "POSITION";
    inputElement.SemanticIndex = 0;
    inputElement.Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElement.InputSlot = 0;
    inputElement.AlignedByteOffset = 0;
    inputElement.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElement.InstanceDataStepRate = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root;
    desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.InputLayout = {&inputElement, 1};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.NodeMask = 0;

    result = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso));
    if (FAILED(result))
    {
        Log(audit.gpu, "CreateGraphicsPipelineState failed 0x%08X", static_cast<unsigned>(result));
        return false;
    }
    return true;
}

bool BuildValidationCamera(const xraylevel12::Geometry& geometry,
                           uint32_t width, uint32_t height,
                           gamecamera12::CameraCaptureInput12& input,
                           std::optional<gamecamera12::GameCameraSnapshot12>& snapshot,
                           Audit& audit)
{
    const DirectX::XMFLOAT3 minimum{geometry.bounds.minimum[0],
                                    geometry.bounds.minimum[1],
                                    geometry.bounds.minimum[2]};
    const DirectX::XMFLOAT3 maximum{geometry.bounds.maximum[0],
                                    geometry.bounds.maximum[1],
                                    geometry.bounds.maximum[2]};
    const DirectX::XMFLOAT3 center{
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f};
    // Deterministic validation camera: three-quarter view of the real
    // visual, still using the X-Ray row-vector View/Projection contract.
    const DirectX::XMFLOAT3 eye{center.x + 1.2f, center.y + 1.2f,
                                center.z + 1.2f};
    const DirectX::XMVECTOR eyeVector = DirectX::XMLoadFloat3(&eye);
    const DirectX::XMVECTOR centerVector = DirectX::XMLoadFloat3(&center);
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const DirectX::XMMATRIX view =
        DirectX::XMMatrixLookAtLH(eyeVector, centerVector, up);
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV4, static_cast<float>(width) / static_cast<float>(height),
        0.1f, 100.0f);
    const DirectX::XMMATRIX viewProjection = view * projection;

    DirectX::XMStoreFloat4x4(&input.view, view);
    DirectX::XMStoreFloat4x4(&input.projection, projection);
    DirectX::XMStoreFloat4x4(&input.fullTransform, viewProjection);
    input.hasFullTransform = true;
    input.position = eye;
    input.sourceGameFrame = 1;
    input.captureSequence = 1;
    input.levelSession = 1;
    input.captureEpoch = 1;
    input.viewportWidth = width;
    input.viewportHeight = height;
    input.sourceMetadata.hasFovY = true;
    input.sourceMetadata.fovYRadians = DirectX::XM_PIDIV4;
    input.sourceMetadata.hasAspectScale = true;
    input.sourceMetadata.aspectScale =
        static_cast<double>(width) / static_cast<double>(height);
    input.sourceMetadata.hasNearZ = true;
    input.sourceMetadata.nearZ = 0.1;
    input.sourceMetadata.hasFarZ = true;
    input.sourceMetadata.farZ = 100.0;

    gamecamera12::CameraCaptureResult12 result =
        gamecamera12::ValidateCamera12(input);
    if (result.error != gamecamera12::CameraError12::None || !result.camera)
    {
        Log(audit.camera, "B2 validation failed error=%d",
            static_cast<int>(result.error));
        return false;
    }
    snapshot = std::move(*result.camera);
    return true;
}

struct CpuProjection
{
    bool any = false;
    float clip[4] = {};
    float ndc[2] = {};
    float pixel[2] = {};
};

CpuProjection ProjectVertex(const DirectX::XMFLOAT4X4& viewProjection,
                            const DirectX::XMFLOAT3& position)
{
    CpuProjection out;
    const DirectX::XMVECTOR input =
        DirectX::XMVectorSet(position.x, position.y, position.z, 1.0f);
    const DirectX::XMVECTOR clip =
        DirectX::XMVector4Transform(input, DirectX::XMLoadFloat4x4(&viewProjection));
    DirectX::XMFLOAT4 clipStorage{};
    DirectX::XMStoreFloat4(&clipStorage, clip);
    out.clip[0] = clipStorage.x;
    out.clip[1] = clipStorage.y;
    out.clip[2] = clipStorage.z;
    out.clip[3] = clipStorage.w;
    if (!(out.clip[3] > 0.0f))
        return out;
    out.ndc[0] = out.clip[0] / out.clip[3];
    out.ndc[1] = out.clip[1] / out.clip[3];
    out.any = std::isfinite(out.ndc[0]) && std::isfinite(out.ndc[1]);
    return out;
}

bool CreateReadbackBuffer(ID3D12Device* device, uint64_t size,
                          ID3D12Resource** resource, Audit& audit)
{
    if (!CreateCommitted(device, Heap(D3D12_HEAP_TYPE_READBACK),
                         BufferDesc(size), D3D12_RESOURCE_STATE_COPY_DEST,
                         nullptr, resource, audit, "readback"))
        return false;
    return true;
}

bool CopyTextureToReadback(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                           ID3D12Resource* texture, ID3D12Resource* readback,
                           uint32_t width, uint32_t height, DXGI_FORMAT format,
                           uint32_t rowPitch, uint64_t totalBytes, Audit& audit)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowSize = 0;
    D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0,
                                  &footprint, &rows, &rowSize, &totalBytes);
    rowPitch = footprint.Footprint.RowPitch;

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;

    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    (void)width;
    (void)height;
    (void)format;
    (void)audit;
    return true;
}

void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

int Run(const Options& options)
{
    Audit audit;
    if (!audit.Open(options.output))
    {
        std::fprintf(stderr, "cannot create output directory\n");
        return 2;
    }

    std::vector<uint8_t> levelBytes;
    std::vector<uint8_t> geomBytes;
    std::string reason;
    if (!ReadFileBytes(options.level, levelBytes, reason) ||
        !ReadFileBytes(options.geom, geomBytes, reason))
    {
        Log(audit.gpu, "%s", reason.c_str());
        std::fprintf(stderr, "%s\n", reason.c_str());
        return 2;
    }

    xraylevel12::Geometry geometry;
    const xraylevel12::ReadError readError = xraylevel12::ReadVisual(
        levelBytes, geomBytes, options.visual, geometry, reason);
    if (readError != xraylevel12::ReadError::None)
    {
        Log(audit.gpu, "B1 ReadVisual failed error=%d reason=%s",
            static_cast<int>(readError), reason.c_str());
        std::fprintf(stderr, "B1 ReadVisual failed: %s\n", reason.c_str());
        return 3;
    }
    for (uint32_t declarationIndex = 0;
         declarationIndex < geometry.declaration_count; ++declarationIndex)
    {
        const auto& element = geometry.declaration[declarationIndex];
        Log(audit.gpu, "decl[%u] stream=%u offset=%u type=%u method=%u usage=%u index=%u",
            declarationIndex, element.stream, element.offset, element.type,
            element.method, element.usage, element.usage_index);
    }
    if (!ValidateGeometry(geometry, reason))
    {
        Log(audit.gpu, "geometry validation failed: %s", reason.c_str());
        std::fprintf(stderr, "geometry validation failed: %s\n", reason.c_str());
        return 3;
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
    Log(audit.geometry, "declaration_count=%u position_offset=0 position_type=FLOAT3",
        geometry.declaration_count);
    for (uint32_t i = 0; i < geometry.vertex_count; ++i)
    {
        DirectX::XMFLOAT3 position{};
        if (!ReadPosition(geometry, i, position, reason))
        {
            Log(audit.gpu, "vertex audit failed: %s", reason.c_str());
            return 3;
        }
        Log(audit.geometry, "vertex[%u]=%.9g %.9g %.9g",
            i, position.x, position.y, position.z);
    }
    for (uint32_t i = 0; i < geometry.index_count; ++i)
        Log(audit.geometry, "index[%u]=%u", i,
            static_cast<unsigned>(geometry.indices[i]));
    uint32_t nonDegenerate = 0;
    double maxArea = 0.0;
    for (uint32_t i = 0; i + 2 < geometry.index_count; i += 3)
    {
        DirectX::XMFLOAT3 a{};
        DirectX::XMFLOAT3 b{};
        DirectX::XMFLOAT3 c{};
        if (!ReadPosition(geometry, geometry.indices[i], a, reason) ||
            !ReadPosition(geometry, geometry.indices[i + 1], b, reason) ||
            !ReadPosition(geometry, geometry.indices[i + 2], c, reason))
        {
            Log(audit.gpu, "triangle audit failed: %s", reason.c_str());
            return 3;
        }
        const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
        const double acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
        const double cx = aby * acz - abz * acy;
        const double cy = abz * acx - abx * acz;
        const double cz = abx * acy - aby * acx;
        const double area = 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
        if (area > 1.0e-10)
            ++nonDegenerate;
        maxArea = std::max(maxArea, area);
    }
    Log(audit.geometry, "non_degenerate_triangles=%u max_triangle_area=%.9g",
        nonDegenerate, maxArea);

    gamecamera12::CameraCaptureInput12 captured{};
    std::optional<gamecamera12::GameCameraSnapshot12> cameraStorage;
    if (!BuildValidationCamera(geometry, options.width, options.height,
                               captured, cameraStorage, audit))
    {
        std::fprintf(stderr, "B2 validation failed\n");
        return 4;
    }
    const gamecamera12::GameCameraSnapshot12* camera = &*cameraStorage;
    Log(audit.camera, "camera_mode=deterministic_validation");
    Log(audit.camera, "real_camera_input=NOT VERIFIED");
    Log(audit.camera, "position=%.9g %.9g %.9g",
        camera->Position().x, camera->Position().y, camera->Position().z);
    Log(audit.camera, "viewport=%ux%u", options.width, options.height);
    Log(audit.camera, "matrix_convention=X-Ray row-vector");
    Log(audit.camera, "shader_contract=row_major HLSL, mul(float4, matrix), no CPU transpose");
    Log(audit.camera, "view:");
    for (uint32_t row = 0; row < 4; ++row)
        Log(audit.camera, "  %.9g %.9g %.9g %.9g",
            camera->View().m[row][0], camera->View().m[row][1],
            camera->View().m[row][2], camera->View().m[row][3]);
    Log(audit.camera, "projection:");
    for (uint32_t row = 0; row < 4; ++row)
        Log(audit.camera, "  %.9g %.9g %.9g %.9g",
            camera->Projection().m[row][0], camera->Projection().m[row][1],
            camera->Projection().m[row][2], camera->Projection().m[row][3]);
    Log(audit.camera, "full_transform:");
    for (uint32_t row = 0; row < 4; ++row)
        Log(audit.camera, "  %.9g %.9g %.9g %.9g",
            camera->ViewProjection().m[row][0], camera->ViewProjection().m[row][1],
            camera->ViewProjection().m[row][2], camera->ViewProjection().m[row][3]);

    DirectX::XMFLOAT3 firstPosition{};
    if (!ReadPosition(geometry, 0, firstPosition, reason))
    {
        Log(audit.gpu, "first position audit failed: %s", reason.c_str());
        return 5;
    }
    const CpuProjection first = ProjectVertex(camera->ViewProjection(), firstPosition);
    Log(audit.camera, "vertex0_clip=%.9g %.9g %.9g %.9g",
        first.clip[0], first.clip[1], first.clip[2], first.clip[3]);
    Log(audit.camera, "vertex0_ndc=%.9g %.9g", first.ndc[0], first.ndc[1]);

    uint32_t cpuMinX = options.width, cpuMinY = options.height;
    uint32_t cpuMaxX = 0, cpuMaxY = 0;
    uint32_t projected = 0;
    for (uint32_t i = 0; i < geometry.vertex_count; ++i)
    {
        DirectX::XMFLOAT3 position{};
        if (!ReadPosition(geometry, i, position, reason))
        {
            Log(audit.gpu, "CPU projection position audit failed: %s",
                reason.c_str());
            return 5;
        }
        const CpuProjection projectedVertex =
            ProjectVertex(camera->ViewProjection(), position);
        if (!projectedVertex.any)
            continue;
        const float px = (projectedVertex.ndc[0] * 0.5f + 0.5f) *
                         static_cast<float>(options.width);
        const float py = (0.5f - projectedVertex.ndc[1] * 0.5f) *
                         static_cast<float>(options.height);
        if (px < 0.0f || py < 0.0f ||
            px > static_cast<float>(options.width) ||
            py > static_cast<float>(options.height))
            continue;
        ++projected;
        cpuMinX = std::min(cpuMinX, static_cast<uint32_t>(px));
        cpuMinY = std::min(cpuMinY, static_cast<uint32_t>(py));
        cpuMaxX = std::max(cpuMaxX, static_cast<uint32_t>(px));
        cpuMaxY = std::max(cpuMaxY, static_cast<uint32_t>(py));
    }
    Log(audit.camera, "cpu_projected_vertices=%u bbox=%u,%u..%u,%u",
        projected, cpuMinX, cpuMinY, cpuMaxX, cpuMaxY);
    if (projected == 0)
    {
        Log(audit.gpu, "validation camera does not project any visual vertex");
        return 5;
    }

    GpuOwner owner;
    auto& device = owner.device;
    auto& queue = owner.queue;
    auto& fence = owner.fence;
    auto& allocator = owner.allocator;
    auto& list = owner.list;
    auto& rtvHeap = owner.rtvHeap;
    auto& dsvHeap = owner.dsvHeap;
    auto& color = owner.color;
    auto& depth = owner.depth;
    auto& vertexBuffer = owner.vertexBuffer;
    auto& indexBuffer = owner.indexBuffer;
    auto& constants = owner.constants;
    auto& clearReadback = owner.clearReadback;
    auto& colorReadback = owner.colorReadback;
    auto& depthReadback = owner.depthReadback;
    auto& root = owner.rootSignature;
    auto& pso = owner.pipelineState;
    auto& uploads = owner.uploads;

    ComPtr<ID3D12Debug> debug;
    bool debugLayerAvailable = false;
    bool gpuBasedValidationRequested = false;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
    {
        debug->EnableDebugLayer();
        debugLayerAvailable = true;
        Log(audit.validation, "debug_layer=enabled");
        ComPtr<ID3D12Debug3> debug3;
        if (SUCCEEDED(debug.As(&debug3)))
        {
            gpuBasedValidationRequested = true;
            debug3->SetEnableGPUBasedValidation(TRUE);
            Log(audit.validation, "gpu_based_validation_requested=1");
            Log(audit.validation, "gpu_based_validation_enable=performed");
        }
        else
        {
            Log(audit.validation, "gpu_based_validation_requested=0");
            Log(audit.validation, "gpu_based_validation_debug3=unavailable");
        }
    }
    else
    {
        Log(audit.validation, "debug_layer=unavailable");
    }
    Log(audit.validation, "debug_layer_available=%d", debugLayerAvailable ? 1 : 0);
    Log(audit.validation, "gbv_requested=%d", gpuBasedValidationRequested ? 1 : 0);

    HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                       IID_PPV_ARGS(device.GetAddressOf()));
    if (FAILED(result))
    {
        Log(audit.gpu, "D3D12CreateDevice failed 0x%08X", static_cast<unsigned>(result));
        std::fprintf(stderr, "D3D12 device creation failed\n");
        return 6;
    }
    Log(audit.validation, "device=hardware");
    Log(audit.validation, "feature_level=11_0");
    LogAdapterInfo(audit);
    Log(audit.validation, "device_removal_reason=0x%08X",
        static_cast<unsigned>(device->GetDeviceRemovedReason()));

    // Make the debug layer honest: clear the storage filter so that messages
    // emitted from now on are stored regardless of severity, and record how
    // many messages were already filtered out before the baseline.
    uint64_t deniedAtStart = 0;
    {
        ComPtr<ID3D12InfoQueue> startupQueue;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(startupQueue.GetAddressOf()))))
        {
            deniedAtStart = startupQueue->GetNumMessagesDeniedByStorageFilter();
            startupQueue->ClearStorageFilter();
            Log(audit.validation, "infoqueue_storage_filter=cleared");
            Log(audit.validation, "infoqueue_denied_at_start=%llu",
                static_cast<unsigned long long>(deniedAtStart));
        }
        else
        {
            Log(audit.validation, "infoqueue_storage_filter=unavailable");
        }
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (!Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue.GetAddressOf())),
               "CreateCommandQueue", audit))
        return 7;

    if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(fence.GetAddressOf())),
               "CreateFence", audit))
        return 7;
    owner.fenceEvent.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!owner.fenceEvent)
    {
        Log(audit.gpu, "CreateEventW failed");
        return 7;
    }

    if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(allocator.GetAddressOf())),
               "CreateCommandAllocator", audit))
        return 7;
    if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.Get(), nullptr,
                                         IID_PPV_ARGS(list.GetAddressOf())),
               "CreateCommandList", audit))
        return 7;
    // CreateCommandList starts in Recording. Close exactly once before the
    // first allocator/list reset. A failed Close is terminal for this command
    // session: no submit, no retry, no reset.
    if (!Check(list->Close(), "Close initial command list", audit))
    {
        owner.listState = ListStateAfterFailedClose();
        return 7;
    }
    owner.listState = ListStateAfterClose();

    GpuFailureContext failure;
    uint64_t fenceValue = 1;
    auto execute = [&](const char* name) -> bool {
        failure.stage = name;
        if (!MaySubmit(owner.listState))
        {
            Log(audit.gpu, "%s rejected: list is not Recording", name);
            return false;
        }
        const HRESULT close = list->Close();
        if (FAILED(close))
        {
            Log(audit.gpu, "%s close failed 0x%08X", name, static_cast<unsigned>(close));
            owner.listState = ListStateAfterFailedClose();
            failure.lastResult = static_cast<long>(close);
            return false;
        }
        owner.listState = ListStateAfterClose();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        owner.workState = StateAfterExecute();
        failure.fenceRequested = fenceValue;
        const HRESULT signal = queue->Signal(fence.Get(), fenceValue);
        failure.lastResult = static_cast<long>(signal);
        if (FAILED(signal))
        {
            Log(audit.gpu, "%s signal failed 0x%08X", name, static_cast<unsigned>(signal));
            return false;
        }
        const FenceOutcome outcome =
            WaitForFence(fence.Get(), owner.fenceEvent.get(), fenceValue, audit);
        const long removedReason =
            static_cast<long>(device->GetDeviceRemovedReason());
        failure.deviceRemovedReason = removedReason;
        owner.workState = StateAfterFenceWait(outcome, removedReason);
        if (outcome != FenceOutcome::Completed)
        {
            Log(audit.gpu, "%s fence timeout/failure removed_reason=0x%08X", name,
                static_cast<unsigned>(removedReason));
            return false;
        }
        ++fenceValue;
        owner.listState = ListStateAfterConfirmedSubmit();
        return true;
    };
    auto reset = [&]() -> bool {
        if (!MayResetList(owner.listState, owner.workState))
        {
            Log(audit.gpu, "reset rejected: list=%d work=%d",
                static_cast<int>(owner.listState), static_cast<int>(owner.workState));
            return false;
        }
        const HRESULT allocatorResult = allocator->Reset();
        if (FAILED(allocatorResult))
        {
            Log(audit.gpu, "Reset allocator failed HRESULT=0x%08X",
                static_cast<unsigned>(allocatorResult));
            Log(audit.gpu, "infoqueue inspection deferred: GPU work state is not clean");
            return false;
        }
        const HRESULT listResult = list->Reset(allocator.Get(), nullptr);
        if (FAILED(listResult))
        {
            Log(audit.gpu, "Reset command list failed HRESULT=0x%08X",
                static_cast<unsigned>(listResult));
            Log(audit.gpu, "infoqueue inspection deferred: GPU work state is not clean");
            return false;
        }
        owner.listState = ListStateAfterReset();
        owner.workState = GpuWorkState::NoWorkSubmitted;
        return true;
    };
    // Unconfirmed completion never returns through Run()/main(): the process
    // exits without C++ destruction, so the live GPU owner is not released.
    auto failAfterSubmit = [&](int code, const char* stage) -> int {
        failure.stage = stage;
        if (MustRetainUntilCompletion(owner.workState))
            TerminalGpuFailure(audit, owner, code, failure);
        return code;
    };
    auto terminalAction = [&]() noexcept {
        TerminalGpuFailure(audit, owner, b3a::kTerminalGpuExitCode, failure);
    };
    b3a::UnknownCompletionGuard guard(owner.workState, terminalAction);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE unusedGpu{};

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    if (!Check(device->CreateDescriptorHeap(&rtvHeapDesc,
                                             IID_PPV_ARGS(rtvHeap.GetAddressOf())),
               "Create RTV heap", audit))
        return 8;
    rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    (void)unusedGpu;

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    if (!Check(device->CreateDescriptorHeap(&dsvHeapDesc,
                                             IID_PPV_ARGS(dsvHeap.GetAddressOf())),
               "Create DSV heap", audit))
        return 8;
    dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();

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

    if (!CreateCommitted(device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         TextureDesc(options.width, options.height,
                                     DXGI_FORMAT_R8G8B8A8_UNORM),
                         D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
                         color.GetAddressOf(), audit, "color target"))
        return 8;
    if (!CreateCommitted(device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         TextureDesc(options.width, options.height,
                                     DXGI_FORMAT_D32_FLOAT),
                         D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                         depth.GetAddressOf(), audit, "depth target"))
        return 8;
    device->CreateRenderTargetView(color.Get(), nullptr, rtvHandle);
    device->CreateDepthStencilView(depth.Get(), nullptr, dsvHandle);

    if (!CreateCommitted(device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         BufferDesc(geometry.vertex_bytes.size()),
                         D3D12_RESOURCE_STATE_COMMON, nullptr,
                         vertexBuffer.GetAddressOf(), audit, "vertex buffer"))
        return 9;
    if (!CreateCommitted(device.Get(), Heap(D3D12_HEAP_TYPE_DEFAULT),
                         BufferDesc(geometry.indices.size() * sizeof(uint16_t)),
                         D3D12_RESOURCE_STATE_COMMON, nullptr,
                         indexBuffer.GetAddressOf(), audit, "index buffer"))
        return 9;
    if (!reset())
        return 9;
    if (!CopyBuffer(device.Get(), list.Get(), vertexBuffer.Get(),
                    geometry.vertex_bytes.data(), geometry.vertex_bytes.size(),
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, audit,
                    "vertex upload", uploads))
        return 9;
    if (!CopyBuffer(device.Get(), list.Get(), indexBuffer.Get(),
                    geometry.indices.data(), geometry.indices.size() * sizeof(uint16_t),
                    D3D12_RESOURCE_STATE_INDEX_BUFFER, audit, "index upload",
                    uploads))
        return 9;
    if (!execute("upload"))
        return failAfterSubmit(9, "upload");
    uploads.clear();
    if (!reset())
        return 9;

    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    vertexView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexView.SizeInBytes = static_cast<UINT>(geometry.vertex_bytes.size());
    vertexView.StrideInBytes = geometry.vertex_stride;
    D3D12_INDEX_BUFFER_VIEW indexView{};
    indexView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexView.SizeInBytes = static_cast<UINT>(geometry.indices.size() * sizeof(uint16_t));
    indexView.Format = DXGI_FORMAT_R16_UINT;

    if (!CreateRootSignature(device.Get(), root.GetAddressOf(), audit))
        return 10;
    if (!CreatePipeline(device.Get(), root.Get(), pso.GetAddressOf(), audit))
        return 10;

    if (!CreateCommitted(device.Get(), Heap(D3D12_HEAP_TYPE_UPLOAD),
                         BufferDesc(256), D3D12_RESOURCE_STATE_GENERIC_READ,
                         nullptr, constants.GetAddressOf(), audit, "constant buffer"))
        return 10;
    void* mappedConstants = nullptr;
    if (!Check(constants->Map(0, nullptr, &mappedConstants), "Map constants", audit))
        return 10;
    std::memcpy(mappedConstants, &camera->ViewProjection(), sizeof(DirectX::XMFLOAT4X4));
    constants->Unmap(0, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(options.width);
    viewport.Height = static_cast<float>(options.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(options.width),
                       static_cast<LONG>(options.height)};

    auto clearPass = [&](bool copyClear) -> bool {
        // The caller has already opened a recording list; the clear pass must
        // not reset a list that is still Recording.
        if (!MaySubmit(owner.listState))
        {
            Log(audit.gpu, "clear pass rejected: list is not Recording");
            return false;
        }
        list->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        list->ClearRenderTargetView(rtvHandle, colorClear.Color, 0, nullptr);
        list->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                    1.0f, 0, 0, nullptr);
        if (!copyClear)
            return execute("clear frame");

        Transition(list.Get(), color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        uint32_t rowPitch = 0;
        uint64_t totalBytes = 0;
        auto& readback = clearReadback;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowSize = 0;
        D3D12_RESOURCE_DESC colorDesc = color->GetDesc();
        device->GetCopyableFootprints(&colorDesc, 0, 1, 0,
                                      &footprint, &rows, &rowSize, &totalBytes);
        rowPitch = footprint.Footprint.RowPitch;
        if (!CreateReadbackBuffer(device.Get(), totalBytes,
                                  readback.GetAddressOf(), audit))
            return false;
        if (!CopyTextureToReadback(device.Get(), list.Get(), color.Get(),
                                   readback.Get(), options.width, options.height,
                                   DXGI_FORMAT_R8G8B8A8_UNORM, rowPitch,
                                   totalBytes, audit))
            return false;
        if (!execute("clear readback"))
            return false;
        void* mapped = nullptr;
        if (!Check(readback->Map(0, nullptr, &mapped), "Map clear readback", audit))
            return false;
        std::vector<uint8_t> pixels(static_cast<size_t>(rowPitch) * options.height);
        std::memcpy(pixels.data(), mapped, pixels.size());
        readback->Unmap(0, nullptr);
        if (!WritePpm(options.output / "offscreen-clear.ppm", options.width,
                      options.height, pixels, rowPitch))
        {
            Log(audit.gpu, "failed to write clear PPM");
            return false;
        }
        uint32_t nonClear = 0;
        for (uint32_t y = 0; y < options.height; ++y)
        {
            const uint8_t* row = pixels.data() + static_cast<size_t>(y) * rowPitch;
            for (uint32_t x = 0; x < options.width; ++x)
            {
                if (row[x * 4] != 13 || row[x * 4 + 1] != 15 ||
                    row[x * 4 + 2] != 20)
                    ++nonClear;
            }
        }
        Log(audit.readback, "clear_only_non_clear_pixels=%u", nonClear);
        if (nonClear != 0)
            return false;
        return true;
    };

    if (!clearPass(true))
        return failAfterSubmit(11, "clear readback");

    if (!reset())
        return 11;
    Transition(list.Get(), color.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    list->ClearRenderTargetView(rtvHandle, colorClear.Color, 0, nullptr);
    list->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                1.0f, 0, 0, nullptr);
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(root.Get());
    list->SetPipelineState(pso.Get());
    list->SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
    list->IASetVertexBuffers(0, 1, &vertexView);
    list->IASetIndexBuffer(&indexView);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawIndexedInstanced(geometry.index_count, 1, geometry.first_index,
                               geometry.base_vertex, 0);
    Log(audit.gpu, "draw=DrawIndexedInstanced index_count=%u first_index=%u base_vertex=%u",
        geometry.index_count, geometry.first_index, geometry.base_vertex);

    Transition(list.Get(), color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list.Get(), depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
               D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorFootprint{};
    UINT colorRows = 0;
    UINT64 colorRowSize = 0;
    uint64_t colorTotal = 0;
    D3D12_RESOURCE_DESC colorDesc = color->GetDesc();
    device->GetCopyableFootprints(&colorDesc, 0, 1, 0,
                                  &colorFootprint, &colorRows, &colorRowSize,
                                  &colorTotal);
    const uint32_t colorPitch = colorFootprint.Footprint.RowPitch;
    if (!CreateReadbackBuffer(device.Get(), colorTotal,
                              colorReadback.GetAddressOf(), audit))
        return 12;
    if (!CopyTextureToReadback(device.Get(), list.Get(), color.Get(),
                               colorReadback.Get(), options.width, options.height,
                               DXGI_FORMAT_R8G8B8A8_UNORM, colorPitch,
                               colorTotal, audit))
        return 12;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT depthFootprint{};
    UINT depthRows = 0;
    UINT64 depthRowSize = 0;
    uint64_t depthTotal = 0;
    D3D12_RESOURCE_DESC depthDesc = depth->GetDesc();
    device->GetCopyableFootprints(&depthDesc, 0, 1, 0,
                                  &depthFootprint, &depthRows, &depthRowSize,
                                  &depthTotal);
    const uint32_t depthPitch = depthFootprint.Footprint.RowPitch;
    if (!CreateReadbackBuffer(device.Get(), depthTotal,
                              depthReadback.GetAddressOf(), audit))
        return 12;
    if (!CopyTextureToReadback(device.Get(), list.Get(), depth.Get(),
                               depthReadback.Get(), options.width, options.height,
                               DXGI_FORMAT_D32_FLOAT, depthPitch,
                               depthTotal, audit))
        return 12;

    if (!execute("draw and readback"))
        return failAfterSubmit(12, "draw and readback");

    void* mappedColor = nullptr;
    if (!Check(colorReadback->Map(0, nullptr, &mappedColor),
               "Map color readback", audit))
        return 13;
    std::vector<uint8_t> colorPixels(static_cast<size_t>(colorPitch) * options.height);
    std::memcpy(colorPixels.data(), mappedColor, colorPixels.size());
    colorReadback->Unmap(0, nullptr);

    if (!WritePpm(options.output / "offscreen-color.ppm", options.width,
                  options.height, colorPixels, colorPitch))
    {
        Log(audit.gpu, "failed to write color PPM");
        return 13;
    }

    uint32_t nonClear = 0;
    uint32_t minX = options.width, minY = options.height, maxX = 0, maxY = 0;
    for (uint32_t y = 0; y < options.height; ++y)
    {
        const uint8_t* row = colorPixels.data() + static_cast<size_t>(y) * colorPitch;
        for (uint32_t x = 0; x < options.width; ++x)
        {
            const bool differs = row[x * 4] != 13 || row[x * 4 + 1] != 15 ||
                                 row[x * 4 + 2] != 20;
            if (differs)
            {
                ++nonClear;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (nonClear == 0)
    {
        Log(audit.gpu, "color target contains only clear color");
        return 13;
    }
    const bool bboxOk = minX + 3 >= cpuMinX && minY + 3 >= cpuMinY &&
                        maxX <= cpuMaxX + 3 && maxY <= cpuMaxY + 3;
    Log(audit.readback, "dimensions=%ux%u format=DXGI_FORMAT_R8G8B8A8_UNORM",
        options.width, options.height);
    Log(audit.readback, "row_pitch=%u", colorPitch);
    Log(audit.readback, "resource_state=RENDER_TARGET->COPY_SOURCE");
    Log(audit.readback, "non_clear_pixels=%u", nonClear);
    Log(audit.readback, "gpu_bbox=%u,%u..%u,%u cpu_bbox=%u,%u..%u,%u bbox_ok=%d",
        minX, minY, maxX, maxY, cpuMinX, cpuMinY, cpuMaxX, cpuMaxY,
        bboxOk ? 1 : 0);
    if (!bboxOk)
    {
        Log(audit.gpu, "GPU color bbox is outside CPU projected bounds");
        return 13;
    }

    void* mappedDepth = nullptr;
    if (!Check(depthReadback->Map(0, nullptr, &mappedDepth),
               "Map depth readback", audit))
        return 13;
    std::vector<float> depthValues(static_cast<size_t>(depthPitch / 4) * options.height);
    std::memcpy(depthValues.data(), mappedDepth, depthValues.size() * sizeof(float));
    depthReadback->Unmap(0, nullptr);
    float depthMin = 1.0f;
    float depthMax = 0.0f;
    for (uint32_t y = 0; y < options.height; ++y)
    {
        const float* row = depthValues.data() +
                           static_cast<size_t>(y) * (depthPitch / 4);
        for (uint32_t x = 0; x < options.width; ++x)
        {
            depthMin = std::min(depthMin, row[x]);
            depthMax = std::max(depthMax, row[x]);
        }
    }
    Log(audit.readback, "depth_format=DXGI_FORMAT_D32_FLOAT depth_row_pitch=%u",
        depthPitch);
    Log(audit.readback, "depth_min=%.9g depth_max=%.9g", depthMin, depthMax);
    if (!(depthMin < 1.0f && depthMax <= 1.0f && depthMin >= 0.0f))
    {
        Log(audit.gpu, "depth test did not produce expected depth values");
        return 13;
    }
    if (!WriteDepthPgm(options.output / "offscreen-depth.pgm", options.width,
                       options.height, depthValues, depthPitch))
    {
        Log(audit.gpu, "failed to write depth PGM");
        return 13;
    }

    const InfoQueueResult infoQueue = InspectInfoQueue(
        device.Get(), audit, CompletionConfirmed(owner.workState),
        debugLayerAvailable, gpuBasedValidationRequested, deniedAtStart);
    if (!b3a::ValidationPasses(infoQueue.status))
    {
        Log(audit.gpu, "frame_status=REJECTED_BY_VALIDATION status=%s",
            infoQueue.status);
        return failAfterSubmit(14, "infoqueue validation");
    }

    Log(audit.gpu, "frame_status=PASS");
    Log(audit.gpu, "geometry=real_xray_jupiter_visual_%u", geometry.visual_id);
    Log(audit.gpu, "draw=verified");
    Log(audit.gpu, "fence=completed");
    Log(audit.gpu, "readback=verified");
    Log(audit.gpu, "dx12_device=hardware");
    Log(audit.gpu, "dx11_game_touched=no");
    Log(audit.validation, "fence_timeout_ms=%u", kFenceTimeoutMs);
    Log(audit.validation, "adapter=hardware");
    Log(audit.validation, "warp_used=no");

    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    // Test-only seam: exercise the real terminal function of this executable
    // without creating a device or submitting GPU work. It proves that the
    // terminal path logs, flushes and exits without C++ destruction.
    if (argc == 3 && std::strcmp(argv[1], "--terminal-selftest") == 0)
    {
        Audit audit;
        if (!audit.Open(fs::path(argv[2])))
            return 22;
        GpuOwner owner;
        owner.workState = GpuWorkState::WorkSubmittedUnknown;
        GpuFailureContext context;
        context.stage = "terminal_selftest";
        context.fenceRequested = 42;
        TerminalGpuFailure(audit, owner, 93, context);
    }

    Options options;
    std::string reason;
    if (!ParseOptions(argc, argv, options, reason))
    {
        std::fprintf(stderr, "%s\n", reason.c_str());
        return 2;
    }
    try
    {
        return Run(options);
    }
    catch (const std::exception& error)
    {
        // Reached only when no GPU owner is alive with unconfirmed work: the
        // in-Run guard already converted that case into a terminal exit.
        std::fprintf(stderr, "unhandled exception: %s\n", error.what());
        return 20;
    }
    catch (...)
    {
        std::fprintf(stderr, "unhandled unknown exception\n");
        return 21;
    }
}
