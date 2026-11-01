// F4.0-B3-A: standalone D3D12 offscreen proof for real B1 X-Ray geometry.
//
// The game executable, CHW12, DX11/R4 and the B1 reader are not modified by
// this test. The test creates its own D3D12 device, command queue and fence,
// uploads the compact B1 visual to default-heap VB/IB buffers, renders to an
// independent color/depth pair, and reads the color/depth data back after a
// bounded GPU fence wait.

#include "b3a_gpu_state.h"
#include "b3b1_visibility.h"
#include "b3b1_sha256.h"
#include "dx12cameracapturefile.h"
#include "dx12gamecamera.h"
#include "dx12xraylevelgeometry.h"
#include "dx12fencewait.h"
#include "dx12lifetime.h"
#include "b3b2_persistent_evidence_validation.h"

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
    fs::path cameraFile;
    std::string levelName;
    uint32_t visual = 3;
    uint32_t width = kDefaultWidth;
    uint32_t height = kDefaultHeight;
    bool widthSpecified = false;
    bool heightSpecified = false;
    bool cameraFileSpecified = false;
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

    bool FlushAndVerify() noexcept
    {
        bool ok = true;
        std::ofstream* streams[] = {&gpu, &geometry, &camera,
                                    &readback, &validation, &infoQueue};
        for (std::ofstream* stream : streams)
        {
            try
            {
                stream->flush();
                if (!*stream)
                    ok = false;
            }
            catch (...)
            {
                ok = false;
            }
        }
        return ok;
    }

    bool CloseAndVerify() noexcept
    {
        bool ok = FlushAndVerify();
        std::ofstream* streams[] = {&gpu, &geometry, &camera,
                                    &readback, &validation, &infoQueue};
        for (std::ofstream* stream : streams)
        {
            try
            {
                if (stream->is_open())
                    stream->close();
                if (stream->fail())
                    ok = false;
            }
            catch (...)
            {
                ok = false;
            }
        }
        return ok;
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

// Single physical record per D3D12 message.  The description is streamed as
// hex so arbitrary payload bytes (LF, CRLF, tabs, backslashes) cannot create
// continuation lines or hit the generic 2 KiB Log buffer.
void LogInfoQueueRecord(std::ofstream& out, uint64_t index, int severity,
                        int id, uint64_t descriptionLength,
                        const char* description)
{
    if (!out)
        return;
    out << "format=v2 index=" << index << " severity=" << severity
        << " id=" << id << " description_length=" << descriptionLength
        << " description_hex=";
    if (description != nullptr &&
        descriptionLength <= b3b2evidence::kMaxInfoQueueDescriptionBytes)
    {
        static const char digits[] = "0123456789ABCDEF";
        char chunk[4096];
        size_t used = 0;
        for (uint64_t i = 0; i < descriptionLength; ++i)
        {
            const unsigned byte = static_cast<unsigned char>(description[i]);
            chunk[used++] = digits[byte >> 4];
            chunk[used++] = digits[byte & 0x0F];
            if (used == sizeof(chunk))
            {
                out.write(chunk, static_cast<std::streamsize>(used));
                used = 0;
            }
        }
        if (used != 0)
            out.write(chunk, static_cast<std::streamsize>(used));
    }
    out << "\n";
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
    const std::string header = "P6\n" + std::to_string(width) + " " +
                               std::to_string(height) + "\n255\n";
    file << header;
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
    file.flush();
    file.close();
    if (!file)
        return false;
    std::error_code ec;
    const uint64_t expected =
        static_cast<uint64_t>(header.size()) +
        3ull * static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    return fs::is_regular_file(path, ec) && !ec &&
           fs::file_size(path, ec) == expected && !ec;
}

bool WriteDepthPgm(const fs::path& path, uint32_t width, uint32_t height,
                   const std::vector<float>& depth, uint32_t rowPitch)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    const std::string header = "P5\n" + std::to_string(width) + " " +
                               std::to_string(height) + "\n255\n";
    file << header;
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
    file.flush();
    file.close();
    if (!file)
        return false;
    std::error_code ec;
    const uint64_t expected =
        static_cast<uint64_t>(header.size()) +
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    return fs::is_regular_file(path, ec) && !ec &&
           fs::file_size(path, ec) == expected && !ec;
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
        else if (arg == "--camera-file")
        {
            if (!next(value))
            {
                reason = "missing value for --camera-file";
                return false;
            }
            options.cameraFile = value;
            options.cameraFileSpecified = true;
        }
        else if (arg == "--level-name")
        {
            if (!next(value))
            {
                reason = "missing value for --level-name";
                return false;
            }
            options.levelName = value;
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
            {
                options.width = parsed;
                options.widthSpecified = true;
            }
            else
            {
                options.height = parsed;
                options.heightSpecified = true;
            }
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
    if (options.cameraFileSpecified)
    {
        if (options.cameraFile.empty())
        {
            reason = "empty --camera-file path";
            return false;
        }
        if (options.levelName.empty())
        {
            reason = "--camera-file requires --level-name for the level match check";
            return false;
        }
    }
    if (options.widthSpecified != options.heightSpecified)
    {
        reason = "--width and --height must be provided together";
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

// Declared POSITION offset of the selected visual. The standalone test is
// single-threaded and reads one visual per process.
uint32_t g_positionOffset = 0;

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
    const uint64_t offset = static_cast<uint64_t>(index) *
                                geometry.vertex_stride +
                            g_positionOffset;
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
bool ValidateGeometry(const xraylevel12::Geometry& geometry,
                      bool strictJupiterShape, std::string& reason)
{
    if (strictJupiterShape)
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
    }
    else
    {
        // Real-camera replay may select any structurally valid real visual.
        if (geometry.vertex_count == 0 || geometry.index_count < 3 ||
            (geometry.index_count % 3) != 0 || geometry.vertex_stride == 0 ||
            geometry.vertex_stride > 4096 ||
            geometry.vertex_bytes.size() !=
                static_cast<uint64_t>(geometry.vertex_count) *
                    geometry.vertex_stride ||
            geometry.indices.size() != geometry.index_count ||
            geometry.first_index > 0xFFFF ||
            geometry.base_vertex > geometry.vertex_count)
        {
            reason = "unexpected compact geometry shape";
            return false;
        }
    }
    const PositionElement declaration = ValidateDeclaration(geometry, reason);
    if (!declaration.found)
        return false;
    if (strictJupiterShape && declaration.offset != 0)
    {
        reason = "Jupiter visual 3 POSITION is not at offset 0";
        return false;
    }
    g_positionOffset = declaration.offset;
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
        LogInfoQueueRecord(
            audit.infoQueue, i, static_cast<int>(message->Severity),
            static_cast<int>(message->ID), message->DescriptionByteLength,
            message->pDescription);
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
                    uint32_t positionOffset, ID3D12PipelineState** pso,
                    Audit& audit)
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
    inputElement.AlignedByteOffset = positionOffset;
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

bool LevelNamesEqual(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr)
        return false;
    while (*left && *right)
    {
        unsigned char a = static_cast<unsigned char>(*left);
        unsigned char b = static_cast<unsigned char>(*right);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

bool BuildRealCameraFromFile(Options& options,
                             gamecamera12::CameraCaptureInput12& input,
                             std::optional<gamecamera12::GameCameraSnapshot12>& snapshot,
                             std::string& captureSha256,
                             Audit& audit)
{
    std::vector<uint8_t> fileBytes;
    std::string reason;
    if (!ReadFileBytes(options.cameraFile, fileBytes, reason))
    {
        Log(audit.camera, "camera_file_error=%s", reason.c_str());
        return false;
    }
    captureSha256 = b3b1::Sha256Hex(fileBytes);

    cameracapture12::CameraCaptureFile12 file{};
    cameracapture12::FileError fileError = cameracapture12::FileError::None;
    if (!cameracapture12::Decode(fileBytes, file, fileError))
    {
        Log(audit.camera, "camera_wire_error=%s (%d)",
            cameracapture12::FileErrorMessage(fileError),
            static_cast<int>(fileError));
        return false;
    }

    if (!LevelNamesEqual(file.levelName, options.levelName.c_str()))
    {
        Log(audit.camera, "camera_level_mismatch file='%s' expected='%s'",
            file.levelName, options.levelName.c_str());
        return false;
    }

    input = file.input;
    gamecamera12::CameraCaptureResult12 result =
        gamecamera12::ValidateCamera12(input);
    if (result.error != gamecamera12::CameraError12::None || !result.camera)
    {
        Log(audit.camera, "camera_validation_error=%d",
            static_cast<int>(result.error));
        return false;
    }
    snapshot = std::move(*result.camera);

    const uint32_t captureWidth = snapshot->ViewportWidth();
    const uint32_t captureHeight = snapshot->ViewportHeight();
    if (options.widthSpecified)
    {
        const double captureAspect = static_cast<double>(captureWidth) /
                                     static_cast<double>(captureHeight);
        const double outputAspect = static_cast<double>(options.width) /
                                    static_cast<double>(options.height);
        if (std::fabs(captureAspect - outputAspect) > 1.0e-3)
        {
            Log(audit.camera,
                "camera_viewport_aspect_mismatch capture=%ux%u output=%ux%u",
                captureWidth, captureHeight, options.width, options.height);
            return false;
        }
    }
    else
    {
        options.width = captureWidth;
        options.height = captureHeight;
    }

    double maxDelta = 0.0;
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            maxDelta = std::max(
                maxDelta,
                std::fabs(static_cast<double>(input.fullTransform.m[row][column]) -
                          static_cast<double>(
                              snapshot->ViewProjection().m[row][column])));

    Log(audit.camera, "camera_mode=real_game_capture_replay");
    Log(audit.camera, "camera_wire_format_version=%u",
        static_cast<unsigned>(cameracapture12::kFormatVersion));
    Log(audit.camera, "camera_capture_sha256=%s", captureSha256.c_str());
    Log(audit.camera, "camera_wire_bytes=%llu",
        static_cast<unsigned long long>(fileBytes.size()));
    Log(audit.camera, "camera_level=%s", file.levelName);
    Log(audit.camera, "level_name_match=1");
    Log(audit.camera, "level_session=%llu",
        static_cast<unsigned long long>(input.levelSession));
    Log(audit.camera, "capture_epoch=%llu",
        static_cast<unsigned long long>(input.captureEpoch));
    Log(audit.camera, "capture_sequence=%llu",
        static_cast<unsigned long long>(input.captureSequence));
    Log(audit.camera, "source_game_frame=%u", input.sourceGameFrame);
    Log(audit.camera, "capture_viewport=%ux%u", captureWidth, captureHeight);
    Log(audit.camera, "output_viewport=%ux%u", options.width, options.height);
    Log(audit.camera, "capture_aspect_match=1");
    Log(audit.camera, "full_transform_max_delta=%.9g", maxDelta);
    Log(audit.camera, "real_camera_input=VERIFIED");
    return true;
}

struct CpuProjection
{
    bool any = false;
    bool depthInRange = false;
    float clip[4] = {};
    float ndc[3] = {};
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
    out.ndc[2] = out.clip[2] / out.clip[3];
    out.any = std::isfinite(out.ndc[0]) && std::isfinite(out.ndc[1]) &&
              std::isfinite(out.ndc[2]);
    // D3D depth range is [0, 1]; fragments outside are clipped by the GPU.
    out.depthInRange = out.ndc[2] >= 0.0f && out.ndc[2] <= 1.0f;
    return out;
}

struct CpuVisibility
{
    bool analysisOk = false;
    bool clippedAny = false;
    uint32_t triangles = 0;
    uint32_t clippedTriangles = 0;
    b3b1::CoverageConfidence confidence =
        b3b1::CoverageConfidence::AnalysisInvalid;
    uint64_t coveredPixels = 0;
    uint64_t interiorPixels = 0;
    // Conservative screen-space bounds of clipped primitives.
    double boundMinX = 0.0;
    double boundMinY = 0.0;
    double boundMaxX = 0.0;
    double boundMaxY = 0.0;
    bool boundsValid = false;
    // Pixel-center coverage bounds produced by the shared rasterizer.
    double coveredMinX = 0.0;
    double coveredMinY = 0.0;
    double coveredMaxX = 0.0;
    double coveredMaxY = 0.0;
    bool coverageBoundsValid = false;
};

const char* ConfidenceName(b3b1::CoverageConfidence confidence) noexcept
{
    switch (confidence)
    {
    case b3b1::CoverageConfidence::GuaranteedInterior:
        return "GUARANTEED_INTERIOR_SAMPLE";
    case b3b1::CoverageConfidence::BoundaryOnly:
        return "BOUNDARY_ONLY_OR_UNCERTAIN";
    case b3b1::CoverageConfidence::NoSampleCoverage:
        return "NO_SAMPLE_COVERAGE";
    case b3b1::CoverageConfidence::OutsideFrustum:
        return "OUTSIDE_FRUSTUM";
    case b3b1::CoverageConfidence::AnalysisInvalid:
        return "ANALYSIS_INVALID";
    }
    return "ANALYSIS_INVALID";
}

CpuVisibility AnalyzeVisibility(const xraylevel12::Geometry& geometry,
                                const DirectX::XMFLOAT4X4& viewProjection,
                                uint32_t width, uint32_t height,
                                std::string& reason)
{
    CpuVisibility visibility;
    visibility.analysisOk = true;
    if (geometry.index_count < 3 || geometry.indices.size() < 3)
    {
        visibility.confidence = b3b1::CoverageConfidence::OutsideFrustum;
        return visibility;
    }

    std::vector<b3b1::ScreenTriangle> screenTriangles;
    screenTriangles.reserve(geometry.index_count / 3);
    const DirectX::XMFLOAT4X4& m = viewProjection;
    bool invalid = false;

    for (size_t index = 0; index + 2 < geometry.indices.size(); index += 3)
    {
        ++visibility.triangles;
        DirectX::XMFLOAT3 position[3];
        for (int i = 0; i < 3; ++i)
        {
            if (!ReadPosition(geometry, geometry.indices[index + i],
                              position[i], reason))
            {
                visibility.analysisOk = false;
                return visibility;
            }
        }

        b3b1::ClipVertex clip[3];
        for (int i = 0; i < 3; ++i)
        {
            const double x = position[i].x;
            const double y = position[i].y;
            const double z = position[i].z;
            clip[i].x = x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + m.m[3][0];
            clip[i].y = x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + m.m[3][1];
            clip[i].z = x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + m.m[3][2];
            clip[i].w = x * m.m[0][3] + y * m.m[1][3] + z * m.m[2][3] + m.m[3][3];
        }

        b3b1::ClipVertex polygon[16];
        int count = 0;
        bool clipInvalid = false;
        if (!b3b1::ClipTriangleToFrustum(clip, polygon, count, clipInvalid))
        {
            if (clipInvalid)
                invalid = true;
            continue;
        }

        bool valid = true;
        double screenX[16] = {};
        double screenY[16] = {};
        for (int i = 0; i < count; ++i)
        {
            if (!(polygon[i].w > 1.0e-9) || !std::isfinite(polygon[i].x) ||
                !std::isfinite(polygon[i].y) || !std::isfinite(polygon[i].z) ||
                !std::isfinite(polygon[i].w))
            {
                valid = false;
                break;
            }
            const double ndcX = polygon[i].x / polygon[i].w;
            const double ndcY = polygon[i].y / polygon[i].w;
            screenX[i] = (ndcX * 0.5 + 0.5) * static_cast<double>(width);
            screenY[i] = (0.5 - ndcY * 0.5) * static_cast<double>(height);
            if (!std::isfinite(screenX[i]) || !std::isfinite(screenY[i]))
            {
                valid = false;
                break;
            }
        }
        if (!valid)
        {
            invalid = true;
            continue;
        }

        visibility.clippedAny = true;
        ++visibility.clippedTriangles;
        for (int i = 0; i < count; ++i)
        {
            if (!visibility.boundsValid)
            {
                visibility.boundsValid = true;
                visibility.boundMinX = screenX[i];
                visibility.boundMaxX = screenX[i];
                visibility.boundMinY = screenY[i];
                visibility.boundMaxY = screenY[i];
            }
            else
            {
                visibility.boundMinX = std::min(visibility.boundMinX, screenX[i]);
                visibility.boundMaxX = std::max(visibility.boundMaxX, screenX[i]);
                visibility.boundMinY = std::min(visibility.boundMinY, screenY[i]);
                visibility.boundMaxY = std::max(visibility.boundMaxY, screenY[i]);
            }
        }
        for (int i = 1; i + 1 < count; ++i)
        {
            b3b1::ScreenTriangle triangle;
            triangle.x[0] = screenX[0];
            triangle.y[0] = screenY[0];
            triangle.z[0] = polygon[0].z / polygon[0].w;
            triangle.x[1] = screenX[i];
            triangle.y[1] = screenY[i];
            triangle.z[1] = polygon[i].z / polygon[i].w;
            triangle.x[2] = screenX[i + 1];
            triangle.y[2] = screenY[i + 1];
            triangle.z[2] = polygon[i + 1].z / polygon[i + 1].w;
            screenTriangles.push_back(triangle);
        }
    }

    if (invalid)
    {
        visibility.confidence = b3b1::CoverageConfidence::AnalysisInvalid;
        visibility.analysisOk = false;
        reason = "visibility clipping produced invalid coordinates";
        return visibility;
    }
    if (!visibility.clippedAny)
    {
        visibility.confidence = b3b1::CoverageConfidence::OutsideFrustum;
        return visibility;
    }

    const b3b1::RasterResult raster = b3b1::RasterizeTriangles(
        screenTriangles.data(), screenTriangles.size(), width, height);
    visibility.confidence = raster.confidence;
    visibility.coveredPixels = raster.coveredPixels;
    visibility.interiorPixels = raster.interiorPixels;
    visibility.coverageBoundsValid = raster.boundsValid;
    visibility.coveredMinX = raster.minX;
    visibility.coveredMinY = raster.minY;
    visibility.coveredMaxX = raster.maxX;
    visibility.coveredMaxY = raster.maxY;
    return visibility;
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
} // namespace
