// F4.0-B3-B1: synthetic rasterization acceptance harness.
//
// Acceptance contract:
//   * hardware GPU execution is mandatory by default;
//   * --cpu-only is an explicit, clearly labelled CPU-only mode;
//   * a completed fence must be proven by GetCompletedValue() reaching the
//     expected value, GetDeviceRemovedReason() == S_OK and successful wait
//     calls; a signaled event alone is never completion;
//   * every submitted resource is owned by a single owner until completion is
//     proven;
//   * D3D12 Debug Layer and GPU-Based Validation are enabled, and the whole
//     InfoQueue (startup plus every case) is inspected;
//   * PASS requires all eight mandatory cases exactly once, exact readback
//     masks/counts/depth/colour, clean validation, confirmed fences and
//     successfully written evidence files.

#include "b3a_gpu_state.h"
#include "b3b1_visibility.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using b3a::GpuWorkState;
using b3a::ListState;

namespace
{
constexpr UINT kSize = 64;
constexpr UINT kPixelCount = kSize * kSize;
constexpr UINT kRowPitch = kSize * 4;
constexpr float kClearColor[4] = {0.05f, 0.06f, 0.08f, 1.0f};
constexpr int kGpuUnavailableExit = 77;
constexpr int kTerminalExit = 78;
constexpr int kEvidenceUnverifiedExit = 3;

int g_cpuChecks = 0;
int g_cpuFailures = 0;
int g_gpuCases = 0;
int g_gpuChecks = 0;
int g_gpuFailures = 0;
int g_validationErrors = 0;
int g_validationUnverified = 0;
int g_skippedCases = 0;
int g_duplicateCases = 0;

bool g_cpuOnly = false;
bool g_simulateNoGpu = false;
bool g_gbvActive = false;
std::string g_outputDirectory;
const char* g_startupStatus = "UNVERIFIED";
const char* g_evidenceStatus = "NOT_WRITTEN";

enum class CaseId : int
{
    A = 0,
    B,
    C,
    D,
    EMinus,
    EZero,
    EPlus,
    F,
    Count
};
constexpr int kCaseCount = static_cast<int>(CaseId::Count);
constexpr uint32_t kAllCasesMask = (1u << kCaseCount) - 1u;
constexpr size_t kExpectedCounts[kCaseCount] = {0, 45, 0, 100, 0, 0, 1, 1770};

uint32_t g_caseExecutedMask = 0;
uint32_t g_caseCompletedMask = 0;
uint32_t g_caseValidationMask = 0;
uint32_t g_caseFenceMask = 0;
std::array<int, kCaseCount> g_caseRuns = {};

enum class EvidenceFault
{
    None = 0,
    Open,
    Write,
    Flush,
    Close,
    Missing
};
EvidenceFault g_evidenceFault = EvidenceFault::None;
bool g_evidenceFaultConsumed = false;

bool g_faultStartupError = false;
bool g_faultStartupCorruption = false;
bool g_faultStartupRetrieval = false;
bool g_faultNoInfoQueue = false;
bool g_faultGbvMissing = false;
std::array<bool, kCaseCount> g_faultCaseError = {};
std::array<bool, kCaseCount> g_faultCaseRetrieval = {};
std::array<bool, kCaseCount> g_faultSkip = {};
std::array<bool, kCaseCount> g_faultDuplicate = {};
std::array<bool, kCaseCount> g_faultRenderFail = {};
std::array<bool, kCaseCount> g_faultEmptyReadback = {};

const char* CaseName(CaseId id)
{
    switch (id)
    {
    case CaseId::A: return "A_top_left";
    case CaseId::B: return "B_interior";
    case CaseId::C: return "C_reversed";
    case CaseId::D: return "D_shared_edge";
    case CaseId::EMinus: return "E_boundary_minus";
    case CaseId::EZero: return "E_boundary_zero";
    case CaseId::EPlus: return "E_boundary_plus";
    case CaseId::F: return "F_clipped";
    default: return "INVALID";
    }
}

bool ParseCaseId(const char* text, CaseId& id)
{
    if (text == nullptr)
        return false;
    if (std::strcmp(text, "A") == 0 || std::strcmp(text, "A_top_left") == 0)
        id = CaseId::A;
    else if (std::strcmp(text, "B") == 0 || std::strcmp(text, "B_interior") == 0)
        id = CaseId::B;
    else if (std::strcmp(text, "C") == 0 || std::strcmp(text, "C_reversed") == 0)
        id = CaseId::C;
    else if (std::strcmp(text, "D") == 0 || std::strcmp(text, "D_shared_edge") == 0)
        id = CaseId::D;
    else if (std::strcmp(text, "E-") == 0 ||
             std::strcmp(text, "EMinus") == 0 ||
             std::strcmp(text, "E_boundary_minus") == 0)
        id = CaseId::EMinus;
    else if (std::strcmp(text, "E0") == 0 ||
             std::strcmp(text, "EZero") == 0 ||
             std::strcmp(text, "E_boundary_zero") == 0)
        id = CaseId::EZero;
    else if (std::strcmp(text, "E+") == 0 ||
             std::strcmp(text, "EPlus") == 0 ||
             std::strcmp(text, "E_boundary_plus") == 0)
        id = CaseId::EPlus;
    else if (std::strcmp(text, "F") == 0 || std::strcmp(text, "F_clipped") == 0)
        id = CaseId::F;
    else
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// Evidence files. Every mandatory file is opened, written, flushed, closed and
// verified before PASS. The fault seam is test-only and explicitly labelled.
// ---------------------------------------------------------------------------
struct EvidenceFile
{
    std::string path;
    std::FILE* file = nullptr;
    bool opened = false;
    bool writeFailed = false;
    bool flushFailed = false;
    bool closeFailed = false;
    bool missing = false;

    bool Open(const std::string& newPath)
    {
        path = newPath;
        if (g_evidenceFault == EvidenceFault::Open && !g_evidenceFaultConsumed)
        {
            g_evidenceFaultConsumed = true;
            return false;
        }
        file = std::fopen(path.c_str(), "wb");
        opened = file != nullptr;
        return opened;
    }

    bool Write(const char* text)
    {
        if (file == nullptr || text == nullptr)
        {
            writeFailed = true;
            return false;
        }
        if (g_evidenceFault == EvidenceFault::Write && !g_evidenceFaultConsumed)
        {
            g_evidenceFaultConsumed = true;
            writeFailed = true;
            return false;
        }
        if (std::fputs(text, file) < 0)
        {
            writeFailed = true;
            return false;
        }
        return true;
    }

    bool WriteF(const char* format, ...)
    {
        if (file == nullptr || format == nullptr)
        {
            writeFailed = true;
            return false;
        }
        if (g_evidenceFault == EvidenceFault::Write && !g_evidenceFaultConsumed)
        {
            g_evidenceFaultConsumed = true;
            writeFailed = true;
            return false;
        }
        va_list args;
        va_start(args, format);
        const int written = std::vfprintf(file, format, args);
        va_end(args);
        if (written < 0)
        {
            writeFailed = true;
            return false;
        }
        return true;
    }

    bool Flush()
    {
        if (file == nullptr)
        {
            flushFailed = true;
            return false;
        }
        if (g_evidenceFault == EvidenceFault::Flush && !g_evidenceFaultConsumed)
        {
            g_evidenceFaultConsumed = true;
            flushFailed = true;
            return false;
        }
        if (std::fflush(file) != 0)
        {
            flushFailed = true;
            return false;
        }
        return true;
    }

    bool Close()
    {
        if (file == nullptr)
            return !closeFailed;
        const bool injected =
            g_evidenceFault == EvidenceFault::Close && !g_evidenceFaultConsumed;
        if (injected)
            g_evidenceFaultConsumed = true;
        const int result = std::fclose(file);
        file = nullptr;
        if (injected || result != 0)
        {
            closeFailed = true;
            // Never leave a PASS-looking artifact behind after a failed close.
            ::DeleteFileA(path.c_str());
            return false;
        }
        return true;
    }

    bool Verify()
    {
        if (g_evidenceFault == EvidenceFault::Missing && !g_evidenceFaultConsumed)
        {
            g_evidenceFaultConsumed = true;
            ::DeleteFileA(path.c_str());
            missing = true;
            return false;
        }
        const DWORD attributes = ::GetFileAttributesA(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            missing = true;
            return false;
        }
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        {
            missing = true;
            return false;
        }
        const ULARGE_INTEGER size{{data.nFileSizeLow, data.nFileSizeHigh}};
        return size.QuadPart > 0;
    }

    bool Ok() const
    {
        return opened && !writeFailed && !flushFailed && !closeFailed && !missing;
    }
};

EvidenceFile g_infoQueueDump;
EvidenceFile g_gpuEvidence;
EvidenceFile g_perCaseResults;
EvidenceFile g_summary;

void CheckCpu(bool condition, const char* what)
{
    ++g_cpuChecks;
    if (!condition)
    {
        ++g_cpuFailures;
        std::printf("FAIL cpu %s\n", what);
    }
}

void CheckGpu(bool condition, const char* what)
{
    ++g_gpuChecks;
    if (!condition)
    {
        ++g_gpuFailures;
        std::printf("FAIL gpu %s\n", what);
    }
}

void Evidence(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    std::printf("\n");
    va_end(args);

    if (g_gpuEvidence.file != nullptr)
    {
        if (g_evidenceFault == EvidenceFault::Write && !g_evidenceFaultConsumed)
        {
            g_evidenceFaultConsumed = true;
            g_gpuEvidence.writeFailed = true;
        }
        else
        {
            va_start(args, format);
            if (std::vfprintf(g_gpuEvidence.file, format, args) < 0)
                g_gpuEvidence.writeFailed = true;
            va_end(args);
            std::fputc('\n', g_gpuEvidence.file);
            if (std::fflush(g_gpuEvidence.file) != 0)
                g_gpuEvidence.flushFailed = true;
        }
    }
}

void PerCase(const char* format, ...)
{
    if (g_perCaseResults.file == nullptr)
        return;
    if (g_evidenceFault == EvidenceFault::Write && !g_evidenceFaultConsumed)
    {
        g_evidenceFaultConsumed = true;
        g_perCaseResults.writeFailed = true;
        return;
    }
    va_list args;
    va_start(args, format);
    if (std::vfprintf(g_perCaseResults.file, format, args) < 0)
        g_perCaseResults.writeFailed = true;
    va_end(args);
    std::fputc('\n', g_perCaseResults.file);
}

const char* ConfidenceName(b3b1::CoverageConfidence value)
{
    switch (value)
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

struct ScreenTriangle
{
    double x[3] = {};
    double y[3] = {};
};

ScreenTriangle MakeTriangle(double x0, double y0, double x1, double y1,
                            double x2, double y2)
{
    ScreenTriangle triangle;
    triangle.x[0] = x0; triangle.y[0] = y0;
    triangle.x[1] = x1; triangle.y[1] = y1;
    triangle.x[2] = x2; triangle.y[2] = y2;
    return triangle;
}

struct CpuCaseResult
{
    b3b1::RasterResult raster;
    std::vector<uint8_t> mask;
};

CpuCaseResult RunCpu(const std::vector<ScreenTriangle>& triangles)
{
    CpuCaseResult result;
    result.mask.assign(kPixelCount, 0);
    std::vector<b3b1::ScreenTriangle> shared;
    shared.reserve(triangles.size());
    for (const ScreenTriangle& triangle : triangles)
    {
        b3b1::ScreenTriangle item;
        for (int i = 0; i < 3; ++i)
        {
            item.x[i] = triangle.x[i];
            item.y[i] = triangle.y[i];
            item.z[i] = 0.5;
        }
        shared.push_back(item);
    }
    result.raster = b3b1::RasterizeTriangles(shared.data(), shared.size(),
                                             kSize, kSize, result.mask.data());
    return result;
}

size_t CountMask(const std::vector<uint8_t>& mask)
{
    size_t count = 0;
    for (uint8_t value : mask)
        count += value != 0 ? 1 : 0;
    return count;
}

uint64_t MaskHash(const std::vector<uint8_t>& mask)
{
    uint64_t hash = 1469598103934665603ull;
    for (uint8_t value : mask)
    {
        hash ^= value != 0 ? 1u : 0u;
        hash *= 1099511628211ull;
    }
    return hash;
}

// Independent fixed reference for the eight synthetic cases. These predicates
// are deliberately not the shared CPU rasterizer: they are hand-derived from
// the case geometry and pinned by kExpectedCounts.
bool ExpectedCovered(CaseId id, int x, int y)
{
    switch (id)
    {
    case CaseId::A:
    case CaseId::C:
    case CaseId::EMinus:
    case CaseId::EZero:
        return false;
    case CaseId::B:
        return x >= 10 && x <= 19 && y >= 10 && y <= 19 && x + y <= 28;
    case CaseId::D:
        return x >= 10 && x <= 19 && y >= 10 && y <= 19;
    case CaseId::EPlus:
        return x == 0 && y == 0;
    case CaseId::F:
        return x >= 0 && x < static_cast<int>(kSize) && y >= 0 &&
               y < static_cast<int>(kSize) && x + y <= 58;
    default:
        return false;
    }
}

std::vector<uint8_t> BuildExpectedMask(CaseId id)
{
    std::vector<uint8_t> mask(kPixelCount, 0);
    for (int y = 0; y < static_cast<int>(kSize); ++y)
        for (int x = 0; x < static_cast<int>(kSize); ++x)
            if (ExpectedCovered(id, x, y))
                mask[static_cast<size_t>(y) * kSize + x] = 1;
    return mask;
}

// ---------------------------------------------------------------------------
// Fence completion policy: the caller must not call StateAfterFenceWait with
// Completed until all of these checks have passed.
// ---------------------------------------------------------------------------
struct CompletionEvidence
{
    bool signalOk = true;
    bool setEventOk = true;
    bool waitOk = true;
    bool getRemovedOk = true;
    uint64_t completed = 0;
    uint64_t expected = 0;
    long removedReason = 0;
};

const char* WorkStateName(GpuWorkState state) noexcept
{
    switch (state)
    {
    case GpuWorkState::NoWorkSubmitted: return "NoWorkSubmitted";
    case GpuWorkState::WorkSubmittedUnknown: return "WorkSubmittedUnknown";
    case GpuWorkState::WorkCompleted: return "WorkCompleted";
    case GpuWorkState::DeviceRemoved: return "DeviceRemoved";
    }
    return "Unknown";
}

GpuWorkState ClassifyCompletion(const CompletionEvidence& evidence) noexcept
{
    if (!evidence.signalOk || !evidence.setEventOk || !evidence.waitOk ||
        !evidence.getRemovedOk)
        return b3a::StateAfterFenceWait(b3a::FenceOutcome::Failed,
                                        evidence.removedReason);
    if (evidence.removedReason != S_OK)
        return b3a::StateAfterFenceWait(b3a::FenceOutcome::Failed,
                                        evidence.removedReason);
    // UINT64_MAX is what D3D12 returns after device removal. It is never a
    // completed value even if a pending event happened to be signaled.
    if (evidence.completed == UINT64_MAX)
        return b3a::StateAfterFenceWait(b3a::FenceOutcome::Failed,
                                        evidence.removedReason);
    if (evidence.completed < evidence.expected)
        return b3a::StateAfterFenceWait(b3a::FenceOutcome::Failed,
                                        evidence.removedReason);
    return b3a::StateAfterFenceWait(b3a::FenceOutcome::Completed,
                                    evidence.removedReason);
}

// ---------------------------------------------------------------------------
// Owner: every submitted GPU resource lives here until the process ends.
// ---------------------------------------------------------------------------
struct HarnessOwner
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    b3a::UniqueHandle event;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> color;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> colorReadback;
    ComPtr<ID3D12Resource> depthReadback;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    std::vector<ComPtr<ID3D12Resource>> submittedBuffers;

    // Debug layer state.
    ComPtr<ID3D12InfoQueue> infoQueue;
    bool debugLayerAvailable = false;
    bool gbvRequested = false;
    bool gbvActive = false;
    uint64_t startupDeniedBeforeClear = 0;
    uint64_t startupDiscardedBeforeClear = 0;
    uint64_t deniedBaseline = 0;
    uint64_t discardedBaseline = 0;

    GpuWorkState workState = GpuWorkState::NoWorkSubmitted;
    ListState listState = ListState::Recording;
    uint64_t fenceValue = 0;
    bool lastFenceConfirmed = false;
};

[[noreturn]] void TerminalFailure(HarnessOwner& owner, const char* stage,
                                  int exitCode) noexcept
{
    try
    {
        const long removed =
            owner.device ? static_cast<long>(owner.device->GetDeviceRemovedReason())
                         : 0;
        const uint64_t completed =
            owner.fence ? owner.fence->GetCompletedValue() : 0;
        const char* state =
            owner.workState == GpuWorkState::DeviceRemoved
                ? "DeviceRemoved"
                : "WorkSubmittedUnknown";
        std::printf("terminal_gpu_state=%s\n", state);
        std::printf("terminal_stage=%s\n", stage ? stage : "unknown");
        std::printf("terminal_fence_requested=%llu\n",
                    static_cast<unsigned long long>(owner.fenceValue));
        std::printf("terminal_fence_completed=%llu\n",
                    static_cast<unsigned long long>(completed));
        std::printf("terminal_removed_reason=0x%08X\n",
                    static_cast<unsigned>(removed));
        std::printf("terminal_exit=%d\n", exitCode);
        std::printf("completion_confirmed=0\nreset_allowed=0\n"
                    "resources_retained=1\ngpu_status=TERMINAL_GPU_FAILURE\n");
        if (g_gpuEvidence.file != nullptr)
        {
            std::fprintf(g_gpuEvidence.file,
                         "terminal_gpu_state=%s\nterminal_stage=%s\n"
                         "terminal_fence_requested=%llu\n"
                         "terminal_fence_completed=%llu\n"
                         "terminal_removed_reason=0x%08X\nterminal_exit=%d\n"
                         "completion_confirmed=0\nreset_allowed=0\n"
                         "resources_retained=1\ngpu_status=TERMINAL_GPU_FAILURE\n",
                         state, stage ? stage : "unknown",
                         static_cast<unsigned long long>(owner.fenceValue),
                         static_cast<unsigned long long>(completed),
                         static_cast<unsigned>(removed), exitCode);
            std::fflush(g_gpuEvidence.file);
        }
        if (g_infoQueueDump.file != nullptr)
            std::fflush(g_infoQueueDump.file);
        if (g_perCaseResults.file != nullptr)
            std::fflush(g_perCaseResults.file);
        std::fflush(nullptr);
    }
    catch (...)
    {
    }
    b3a::TerminalExit(exitCode);
}

// ---------------------------------------------------------------------------
// InfoQueue validation. Startup and per-case ranges are distinct; every
// message in the inspected range is enumerated and written to the raw dump.
// ---------------------------------------------------------------------------
struct CaseValidation
{
    bool sampled = false;
    bool complete = false;
    uint64_t errors = 0;
    uint64_t corruptions = 0;
    uint64_t warnings = 0;
    uint64_t retrievalFailures = 0;
    uint64_t hidden = 0;
    uint64_t discarded = 0;
    bool pass = false;
    const char* status = "UNVERIFIED";
};

void DumpMessage(const D3D12_MESSAGE* message)
{
    if (g_infoQueueDump.file == nullptr || message == nullptr)
        return;
    g_infoQueueDump.WriteF("severity=%d id=%d description=%s\n",
                           static_cast<int>(message->Severity),
                           static_cast<int>(message->ID),
                           message->pDescription ? message->pDescription : "");
}

CaseValidation InspectRange(HarnessOwner& owner, uint64_t startIndex,
                            const char* label, bool startup,
                            CaseId caseId)
{
    CaseValidation validation;
    if (!owner.infoQueue)
    {
        validation.status = "UNVERIFIED";
        Evidence("validation=%s errors=0 corruption=0 warnings=0 "
                 "retrieval_failures=0 hidden=0 discarded=0 "
                 "status=UNVERIFIED queue=unavailable",
                 label);
        return validation;
    }
    validation.sampled = true;
    const UINT64 stored = owner.infoQueue->GetNumStoredMessages();
    if (startIndex > stored)
    {
        validation.complete = false;
        validation.retrievalFailures = 1;
        validation.status = "UNVERIFIED";
        Evidence("validation=%s errors=0 corruption=0 warnings=0 "
                 "retrieval_failures=1 status=UNVERIFIED",
                 label);
        return validation;
    }

    const UINT64 deniedNow = owner.infoQueue->GetNumMessagesDeniedByStorageFilter();
    const UINT64 discardedNow =
        owner.infoQueue->GetNumMessagesDiscardedByMessageCountLimit();
    validation.hidden =
        deniedNow > owner.deniedBaseline ? deniedNow - owner.deniedBaseline : 0;
    validation.discarded = discardedNow > owner.discardedBaseline
                               ? discardedNow - owner.discardedBaseline
                               : 0;

    const bool forceRetrievalFailure =
        (startup && g_faultStartupRetrieval) ||
        (!startup && caseId >= CaseId::A && caseId < CaseId::Count &&
         g_faultCaseRetrieval[static_cast<int>(caseId)]);

    for (UINT64 index = startIndex; index < stored; ++index)
    {
        if (forceRetrievalFailure)
        {
            ++validation.retrievalFailures;
            continue;
        }
        SIZE_T size = 0;
        if (FAILED(owner.infoQueue->GetMessage(index, nullptr, &size)) || size == 0)
        {
            ++validation.retrievalFailures;
            continue;
        }
        std::vector<uint8_t> storage(size + alignof(D3D12_MESSAGE) - 1);
        void* aligned = storage.data();
        size_t space = storage.size();
        if (!std::align(alignof(D3D12_MESSAGE), size, aligned, space))
        {
            ++validation.retrievalFailures;
            continue;
        }
        auto* message = static_cast<D3D12_MESSAGE*>(aligned);
        SIZE_T actual = size;
        if (FAILED(owner.infoQueue->GetMessage(index, message, &actual)))
        {
            ++validation.retrievalFailures;
            continue;
        }
        if (!g_faultGbvMissing && message->ID == 1016 &&
            message->pDescription != nullptr &&
            std::strstr(message->pDescription, "GPU-Based Validation") != nullptr)
        {
            if (!owner.gbvActive)
                Evidence("gbv_active_evidence=ID1016");
            owner.gbvActive = true;
        }
        switch (message->Severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION: ++validation.corruptions; break;
        case D3D12_MESSAGE_SEVERITY_ERROR: ++validation.errors; break;
        case D3D12_MESSAGE_SEVERITY_WARNING: ++validation.warnings; break;
        default: break;
        }
        DumpMessage(message);
    }

    validation.complete = validation.retrievalFailures == 0;
    if (!validation.complete || validation.hidden != 0 ||
        validation.discarded != 0)
        validation.status = "UNVERIFIED";
    else if (validation.errors != 0 || validation.corruptions != 0)
        validation.status = "FAIL";
    else
        validation.status = "PASS";
    validation.pass = validation.status[0] == 'P' &&
                      validation.status[1] == 'A' &&
                      validation.status[2] == 'S' &&
                      validation.status[3] == 'S';

    Evidence("validation=%s errors=%llu corruption=%llu warnings=%llu "
             "retrieval_failures=%llu hidden=%llu discarded=%llu status=%s",
             label,
             static_cast<unsigned long long>(validation.errors),
             static_cast<unsigned long long>(validation.corruptions),
             static_cast<unsigned long long>(validation.warnings),
             static_cast<unsigned long long>(validation.retrievalFailures),
             static_cast<unsigned long long>(validation.hidden),
             static_cast<unsigned long long>(validation.discarded),
             validation.status);
    return validation;
}

// ---------------------------------------------------------------------------
// CPU rule tests (A-G), numeric safety and completion policy.
// ---------------------------------------------------------------------------
void RunCompletionTests()
{
    auto classify = [](bool signal, bool setEvent, bool wait, bool getRemoved,
                       uint64_t completed, uint64_t expected,
                       long removed) {
        CompletionEvidence evidence;
        evidence.signalOk = signal;
        evidence.setEventOk = setEvent;
        evidence.waitOk = wait;
        evidence.getRemovedOk = getRemoved;
        evidence.completed = completed;
        evidence.expected = expected;
        evidence.removedReason = removed;
        return ClassifyCompletion(evidence);
    };

    CheckCpu(classify(true, true, true, true, 10, 10, S_OK) ==
                 GpuWorkState::WorkCompleted,
             "completed fence + S_OK is completion");
    CheckCpu(classify(true, true, true, true, 9, 10, S_OK) ==
                 GpuWorkState::WorkSubmittedUnknown,
             "pending fence is not completion");
    CheckCpu(classify(true, true, true, true, UINT64_MAX, 10,
                      static_cast<long>(0x887A0005)) ==
                 GpuWorkState::DeviceRemoved,
             "UINT64_MAX + DEVICE_REMOVED is DeviceRemoved");
    CheckCpu(classify(true, true, true, true, UINT64_MAX, 10,
                      static_cast<long>(0x887A0006)) ==
                 GpuWorkState::DeviceRemoved,
             "UINT64_MAX + DEVICE_HUNG is DeviceRemoved");
    CheckCpu(classify(true, true, true, true, UINT64_MAX, 10, S_OK) ==
                 GpuWorkState::WorkSubmittedUnknown,
             "UINT64_MAX without a removal reason stays unknown");
    CheckCpu(classify(true, true, true, true, 10, 10,
                      static_cast<long>(0x887A0005)) ==
                 GpuWorkState::DeviceRemoved,
             "device removal is never completion even with a matching value");
    CheckCpu(classify(true, false, true, true, 10, 10, S_OK) ==
                 GpuWorkState::WorkSubmittedUnknown,
             "SetEventOnCompletion failure is not completion");
    CheckCpu(classify(false, true, true, true, 10, 10, S_OK) ==
                 GpuWorkState::WorkSubmittedUnknown,
             "Signal failure is not completion");
    CheckCpu(classify(true, true, false, true, 10, 10, S_OK) ==
                 GpuWorkState::WorkSubmittedUnknown,
             "wait timeout is not completion");
    CheckCpu(classify(true, true, true, false, 10, 10, S_OK) ==
                 GpuWorkState::WorkSubmittedUnknown,
             "GetDeviceRemovedReason failure is not completion");

    const GpuWorkState unknown =
        classify(true, true, true, true, 9, 10, S_OK);
    CheckCpu(b3a::MustRetainUntilCompletion(unknown),
             "unknown completion retains GPU resources");
    CheckCpu(!b3a::MayResetList(ListState::ResetReady, unknown),
             "unknown completion forbids reset");
    const GpuWorkState removed =
        classify(true, true, true, true, UINT64_MAX, 10,
                 static_cast<long>(0x887A0005));
    CheckCpu(b3a::MustRetainUntilCompletion(removed),
             "device removal retains GPU resources");
    CheckCpu(!b3a::MayResetList(ListState::ResetReady, removed),
             "device removal forbids reset");
    CheckCpu(b3a::CompletionConfirmed(
                 classify(true, true, true, true, 10, 10, S_OK)),
             "only fully proven completion is confirmed");

    struct FaultCase
    {
        const char* name;
        CompletionEvidence evidence;
    };
    const FaultCase faults[] = {
        {"completed_s_ok", {true, true, true, true, 10, 10, S_OK}},
        {"pending_s_ok", {true, true, true, true, 9, 10, S_OK}},
        {"uint64max_device_removed",
         {true, true, true, true, UINT64_MAX, 10,
          static_cast<long>(0x887A0005)}},
        {"uint64max_device_hung",
         {true, true, true, true, UINT64_MAX, 10,
          static_cast<long>(0x887A0006)}},
        {"signaled_event_but_pending", {true, true, true, true, 9, 10, S_OK}},
        {"set_event_failed", {true, false, true, true, 10, 10, S_OK}},
        {"signal_failed", {false, true, true, true, 10, 10, S_OK}},
        {"wait_timeout", {true, true, false, true, 10, 10, S_OK}},
        {"get_removed_failed", {true, true, true, false, 10, 10, S_OK}}};
    for (const FaultCase& fault : faults)
    {
        const GpuWorkState state = ClassifyCompletion(fault.evidence);
        Evidence("completion_fault=%s state=%s completion_confirmed=%d "
                 "reset_allowed=%d resources_retained=%d",
                 fault.name, WorkStateName(state),
                 b3a::CompletionConfirmed(state) ? 1 : 0,
                 b3a::MayResetList(ListState::ResetReady, state) ? 1 : 0,
                 b3a::MustRetainUntilCompletion(state) ? 1 : 0);
    }
}

void RunNumericSafetyTests()
{
    const auto offscreen = RunCpu(
        {MakeTriangle(1.0e50, 1.0e50, 1.0e50 + 10.0, 1.0e50,
                      1.0e50, 1.0e50 + 10.0)});
    CheckCpu(offscreen.raster.confidence ==
                 b3b1::CoverageConfidence::OutsideFrustum,
             "1e50 fully off-screen is OUTSIDE_FRUSTUM, not ANALYSIS_INVALID");

    const auto huge = RunCpu(
        {MakeTriangle(-1.0e50, -1.0e50, 1.0e50, -1.0e50,
                      0.0, 1.0e50)});
    CheckCpu(huge.raster.confidence ==
                 b3b1::CoverageConfidence::GuaranteedInterior,
             "huge finite triangle covering the viewport is valid");
    CheckCpu(huge.raster.coveredPixels == kPixelCount,
             "huge covering triangle covers every pixel");

    const auto nearInt = RunCpu(
        {MakeTriangle(2147483000.0, 10.0, 2147483001.0, 10.0,
                      2147483000.0, 11.0)});
    CheckCpu(nearInt.raster.confidence ==
                 b3b1::CoverageConfidence::OutsideFrustum,
             "INT_MAX-adjacent coordinates are safely off-screen");

    b3b1::ScreenTriangle valid = {};
    valid.x[0] = 10; valid.y[0] = 10;
    valid.x[1] = 20; valid.y[1] = 10;
    valid.x[2] = 10; valid.y[2] = 20;
    uint8_t mask[kPixelCount] = {};
    const auto hugeWidth = b3b1::RasterizeTriangles(
        &valid, 1, 0x80000000u, kSize, mask);
    CheckCpu(hugeWidth.confidence == b3b1::CoverageConfidence::AnalysisInvalid,
             "width 0x80000000 is rejected before int conversion");
    const auto hugeHeight = b3b1::RasterizeTriangles(
        &valid, 1, kSize, 0x80000000u, mask);
    CheckCpu(hugeHeight.confidence == b3b1::CoverageConfidence::AnalysisInvalid,
             "height 0x80000000 is rejected before int conversion");
    const auto zeroWidth = b3b1::RasterizeTriangles(&valid, 1, 0, kSize, mask);
    CheckCpu(zeroWidth.confidence == b3b1::CoverageConfidence::AnalysisInvalid,
             "zero viewport width is invalid");
    const auto zeroHeight = b3b1::RasterizeTriangles(&valid, 1, kSize, 0, mask);
    CheckCpu(zeroHeight.confidence == b3b1::CoverageConfidence::AnalysisInvalid,
             "zero viewport height is invalid");
    const auto overflow = b3b1::RasterizeTriangles(
        &valid, 1, 0x10000u, 0x10000u, mask);
    CheckCpu(overflow.confidence == b3b1::CoverageConfidence::AnalysisInvalid,
             "oversized width x height is rejected");

    b3b1::ScreenTriangle nanTriangle = valid;
    nanTriangle.x[0] = NAN;
    const auto nanResult = b3b1::RasterizeTriangles(
        &nanTriangle, 1, kSize, kSize, mask);
    CheckCpu(nanResult.confidence == b3b1::CoverageConfidence::AnalysisInvalid,
             "NaN coordinate is ANALYSIS_INVALID");
    b3b1::ScreenTriangle infTriangle = valid;
    infTriangle.y[1] = INFINITY;
    const auto infResult = b3b1::RasterizeTriangles(
        &infTriangle, 1, kSize, kSize, mask);
    CheckCpu(infResult.confidence == b3b1::CoverageConfidence::AnalysisInvalid,
             "Inf coordinate is ANALYSIS_INVALID");

    b3b1::ScreenTriangle degenerate = {};
    degenerate.x[0] = 10; degenerate.y[0] = 10;
    degenerate.x[1] = 10; degenerate.y[1] = 10;
    degenerate.x[2] = 10; degenerate.y[2] = 10;
    const auto degenerateResult = b3b1::RasterizeTriangles(
        &degenerate, 1, kSize, kSize, mask);
    CheckCpu(degenerateResult.confidence ==
                 b3b1::CoverageConfidence::OutsideFrustum,
             "degenerate triangle is OUTSIDE_FRUSTUM");
}

void RunCpuTests()
{
    // Fixed expected masks are independent reference data; verify their
    // pinned counts and cross-check the shared CPU classifier against them.
    for (int i = 0; i < kCaseCount; ++i)
    {
        const CaseId id = static_cast<CaseId>(i);
        const std::vector<uint8_t> expected = BuildExpectedMask(id);
        CheckCpu(CountMask(expected) == kExpectedCounts[i],
                 "fixed expected mask count matches the pinned table");
    }

    {
        const auto cpu = RunCpu({MakeTriangle(0, 0, 1, 0, 0, 1)});
        CheckCpu(cpu.raster.confidence ==
                     b3b1::CoverageConfidence::NoSampleCoverage,
                 "A top-left has no sample coverage");
        CheckCpu(cpu.raster.coveredPixels == 0, "A top-left owns no pixel");
        const auto reversed = RunCpu({MakeTriangle(0, 0, 0, 1, 1, 0)});
        CheckCpu(reversed.raster.confidence ==
                     b3b1::CoverageConfidence::NoSampleCoverage,
                 "C reversed winding keeps ownership");
    }
    {
        const auto cpu = RunCpu({MakeTriangle(10, 10, 20, 10, 10, 20)});
        CheckCpu(cpu.raster.confidence ==
                     b3b1::CoverageConfidence::GuaranteedInterior,
                 "B interior is guaranteed");
        CheckCpu(cpu.raster.interiorPixels > 0, "B has interior pixels");
        CheckCpu(cpu.mask[13 * kSize + 13] != 0, "B covers pixel (13,13)");
        CheckCpu(CountMask(cpu.mask) == 45, "B covers exactly 45 pixels");
        CheckCpu(cpu.mask == BuildExpectedMask(CaseId::B),
                 "B CPU mask equals the independent fixed mask");
    }
    {
        const auto cpu = RunCpu({MakeTriangle(10, 10, 20, 10, 10, 20),
                                 MakeTriangle(20, 10, 20, 20, 10, 20)});
        CheckCpu(cpu.raster.confidence ==
                     b3b1::CoverageConfidence::GuaranteedInterior,
                 "D shared edge has interior");
        CheckCpu(cpu.mask[15 * kSize + 15] != 0, "D covers (15,15)");
        CheckCpu(CountMask(cpu.mask) == 100, "D union covers 100 pixels");
        CheckCpu(cpu.mask == BuildExpectedMask(CaseId::D),
                 "D CPU mask equals the independent fixed mask");
    }
    {
        for (int delta = -2; delta <= 2; ++delta)
        {
            const double shift = delta / 256.0;
            const auto cpu = RunCpu(
                {MakeTriangle(0, 0, 1.0 + shift, 0, 0, 1.0 + shift)});
            if (delta <= 0)
                CheckCpu(cpu.raster.confidence ==
                             b3b1::CoverageConfidence::NoSampleCoverage,
                         "E negative/zero delta needs no coverage");
            else
                CheckCpu(cpu.raster.confidence ==
                             b3b1::CoverageConfidence::BoundaryOnly,
                         "E positive delta is boundary-only");
            Evidence("cpu_case=E delta=%d confidence=%s covered=%llu", delta,
                     ConfidenceName(cpu.raster.confidence),
                     static_cast<unsigned long long>(cpu.raster.coveredPixels));
        }
        const auto positive = RunCpu(
            {MakeTriangle(0, 0, 1.0 + 1.0 / 256.0, 0, 0, 1.0 + 1.0 / 256.0)});
        CheckCpu(CountMask(positive.mask) == 1,
                 "E positive CPU mask covers exactly one pixel");
        CheckCpu(positive.mask == BuildExpectedMask(CaseId::EPlus),
                 "E positive CPU mask equals the independent fixed mask");
        const auto zero = RunCpu(
            {MakeTriangle(0, 0, 1.0, 0, 0, 1.0)});
        CheckCpu(CountMask(zero.mask) == 0, "E zero owns no pixel");
    }
    {
        const auto cpu = RunCpu(
            {MakeTriangle(-40, -40, 100, -40, -40, 100)});
        CheckCpu(cpu.raster.confidence ==
                     b3b1::CoverageConfidence::GuaranteedInterior,
                 "F clipped interior is guaranteed");
        CheckCpu(CountMask(cpu.mask) == 1770,
                 "F CPU mask covers exactly 1770 pixels");
        CheckCpu(cpu.mask == BuildExpectedMask(CaseId::F),
                 "F CPU mask equals the independent fixed mask");
    }
    {
        b3b1::ClipVertex clip[3] = {{-2.0, -2.0, 0.5, 1.0},
                                    {3.0, -2.0, 0.5, 1.0},
                                    {0.0, 3.0, 0.5, 1.0}};
        b3b1::ClipVertex polygon[16];
        int count = 0;
        bool invalid = false;
        const bool clipped =
            b3b1::ClipTriangleToFrustum(clip, polygon, count, invalid);
        CheckCpu(clipped && !invalid, "F clipping succeeds");
        CheckCpu(count == 5, "F clipped polygon has five vertices");
        bool inequalities = true;
        bool finite = true;
        for (int i = 0; i < count; ++i)
        {
            const auto& v = polygon[i];
            finite = finite && std::isfinite(v.x) && std::isfinite(v.y) &&
                     std::isfinite(v.z) && std::isfinite(v.w);
            inequalities = inequalities && v.x >= -v.w - 1.0e-9 &&
                           v.x <= v.w + 1.0e-9 && v.y >= -v.w - 1.0e-9 &&
                           v.y <= v.w + 1.0e-9 && v.z >= -1.0e-9 &&
                           v.z <= v.w + 1.0e-9;
        }
        CheckCpu(finite, "F clipped coordinates are finite");
        CheckCpu(inequalities, "F clipped coordinates satisfy clip inequalities");
        Evidence("cpu_case=F clipped_vertices=%d", count);
    }
    {
        const auto cpu = RunCpu({MakeTriangle(NAN, 0, 1, 0, 0, 1)});
        CheckCpu(cpu.raster.confidence ==
                     b3b1::CoverageConfidence::AnalysisInvalid,
                 "G NaN yields ANALYSIS_INVALID");

        std::vector<b3b1::ClipVertex> polygon(16);
        for (int i = 0; i < 16; ++i)
            polygon[i] = {(i % 2 == 0) ? 0.0 : -2.0, 0.0, 0.5, 1.0};
        std::vector<b3b1::ClipVertex> output(16);
        const int count =
            b3b1::ClipPolygonAgainstPlane(polygon.data(), 16, 0, output.data());
        CheckCpu(count == -1, "G clipping capacity overflow is rejected");

        b3b1::ClipVertex tiny[3] = {{0.0, 0.0, 0.5e-12, 1.0e-12},
                                    {0.0, 0.0, 0.5e-12, 1.0e-12},
                                    {0.0, 0.0, 0.5e-12, 1.0e-12}};
        b3b1::ClipVertex tinyOut[16];
        int tinyCount = 0;
        bool tinyInvalid = false;
        (void)b3b1::ClipTriangleToFrustum(tiny, tinyOut, tinyCount, tinyInvalid);
        CheckCpu(!tinyInvalid, "G near-zero-w clipping is not a numerical error");
        CheckCpu(tinyCount == 0 || tinyCount >= 3, "G near-zero-w result is consistent");
    }

    RunNumericSafetyTests();
    RunCompletionTests();
}

// ---------------------------------------------------------------------------
// D3D12 harness.
// ---------------------------------------------------------------------------
bool InitializeDebugLayer(HarnessOwner& owner)
{
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
    {
        Evidence("debug_layer=unavailable");
        return false;
    }
    debug->EnableDebugLayer();
    owner.debugLayerAvailable = true;
    ComPtr<ID3D12Debug3> debug3;
    if (SUCCEEDED(debug.As(&debug3)))
    {
        owner.gbvRequested = true;
        debug3->SetEnableGPUBasedValidation(TRUE);
        Evidence("gbv_requested=1");
    }
    Evidence("debug_layer=enabled");
    return true;
}

bool InitializeDevice(HarnessOwner& owner)
{
    auto fail = [](const char* step, HRESULT hr) {
        Evidence("gpu_init_fail=%s hr=0x%08X", step,
                 static_cast<unsigned>(hr));
        Evidence("gpu_available=0");
        Evidence("gpu_status=UNVERIFIED");
        return false;
    };
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   IID_PPV_ARGS(owner.device.GetAddressOf()));
    if (FAILED(hr)) return fail("device", hr);

    {
        ComPtr<IDXGIFactory1> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (SUCCEEDED(factory->EnumAdapters1(0, adapter.GetAddressOf())))
            {
                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(adapter->GetDesc1(&desc)))
                {
                    char name[256] = {};
                    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name,
                                        sizeof(name) - 1, nullptr, nullptr);
                    Evidence("adapter=%s vendor=0x%04X device=0x%04X software=%d",
                             name, static_cast<unsigned>(desc.VendorId),
                             static_cast<unsigned>(desc.DeviceId),
                             (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? 1 : 0);
                }
            }
        }
    }

    if (!g_faultNoInfoQueue &&
        SUCCEEDED(owner.device->QueryInterface(
            IID_PPV_ARGS(owner.infoQueue.GetAddressOf()))))
    {
        owner.startupDeniedBeforeClear =
            owner.infoQueue->GetNumMessagesDeniedByStorageFilter();
        owner.startupDiscardedBeforeClear =
            owner.infoQueue->GetNumMessagesDiscardedByMessageCountLimit();
        owner.infoQueue->ClearStorageFilter();
        owner.deniedBaseline =
            owner.infoQueue->GetNumMessagesDeniedByStorageFilter();
        owner.discardedBaseline =
            owner.infoQueue->GetNumMessagesDiscardedByMessageCountLimit();
        Evidence("infoqueue=available denied_before_clear=%llu "
                 "discarded_before_clear=%llu",
                 static_cast<unsigned long long>(owner.startupDeniedBeforeClear),
                 static_cast<unsigned long long>(owner.startupDiscardedBeforeClear));
    }
    else
    {
        owner.infoQueue.Reset();
        Evidence("infoqueue=unavailable");
        g_infoQueueDump.Write("infoqueue=unavailable\n");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = owner.device->CreateCommandQueue(&queueDesc,
                                          IID_PPV_ARGS(owner.queue.GetAddressOf()));
    if (FAILED(hr)) return fail("queue", hr);
    hr = owner.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(owner.fence.GetAddressOf()));
    if (FAILED(hr)) return fail("fence", hr);
    owner.event.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!owner.event) return fail("event", HRESULT_FROM_WIN32(::GetLastError()));

    hr = owner.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(owner.allocator.GetAddressOf()));
    if (FAILED(hr)) return fail("allocator", hr);
    hr = owner.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         owner.allocator.Get(), nullptr,
                                         IID_PPV_ARGS(owner.list.GetAddressOf()));
    if (FAILED(hr)) return fail("list", hr);
    hr = owner.list->Close();
    if (FAILED(hr)) return fail("initial close", hr);
    owner.listState = ListState::Closed;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    hr = owner.device->CreateDescriptorHeap(&rtvDesc,
                                            IID_PPV_ARGS(owner.rtvHeap.GetAddressOf()));
    if (FAILED(hr)) return fail("rtv heap", hr);
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    hr = owner.device->CreateDescriptorHeap(&dsvDesc,
                                            IID_PPV_ARGS(owner.dsvHeap.GetAddressOf()));
    if (FAILED(hr)) return fail("dsv heap", hr);

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = kSize;
    colorDesc.Height = kSize;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::memcpy(clear.Color, kClearColor, sizeof(kClearColor));
    hr = owner.device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &colorDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
        IID_PPV_ARGS(owner.color.GetAddressOf()));
    if (FAILED(hr)) return fail("color", hr);

    D3D12_RESOURCE_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    hr = owner.device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
        IID_PPV_ARGS(owner.depth.GetAddressOf()));
    if (FAILED(hr)) return fail("depth", hr);

    owner.device->CreateRenderTargetView(
        owner.color.Get(), nullptr,
        owner.rtvHeap->GetCPUDescriptorHandleForHeapStart());
    owner.device->CreateDepthStencilView(
        owner.depth.Get(), nullptr,
        owner.dsvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = static_cast<UINT64>(kSize) * kRowPitch;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = owner.device->CreateCommittedResource(
        &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(owner.colorReadback.GetAddressOf()));
    if (FAILED(hr)) return fail("color readback", hr);
    hr = owner.device->CreateCommittedResource(
        &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(owner.depthReadback.GetAddressOf()));
    if (FAILED(hr)) return fail("depth readback", hr);

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                     signature.GetAddressOf(),
                                     error.GetAddressOf());
    if (FAILED(hr)) return fail("serialize root", hr);
    hr = owner.device->CreateRootSignature(0, signature->GetBufferPointer(),
                                           signature->GetBufferSize(),
                                           IID_PPV_ARGS(owner.root.GetAddressOf()));
    if (FAILED(hr)) return fail("root signature", hr);

    static const char* shaderSource = R"(
        struct VSInput { float3 position : POSITION; };
        float4 VSMain(VSInput input) : SV_POSITION
        {
            return float4(input.position, 1.0f);
        }
        float4 PSMain() : SV_TARGET
        {
            return float4(0.0f, 1.0f, 0.0f, 1.0f);
        }
    )";
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    hr = D3DCompile(shaderSource, std::strlen(shaderSource), nullptr, nullptr,
                    nullptr, "VSMain", "vs_5_0", 0, 0, vs.GetAddressOf(),
                    error.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("compile vs", hr);
    hr = D3DCompile(shaderSource, std::strlen(shaderSource), nullptr, nullptr,
                    nullptr, "PSMain", "ps_5_0", 0, 0, ps.GetAddressOf(),
                    error.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail("compile ps", hr);

    D3D12_INPUT_ELEMENT_DESC input{};
    input.SemanticName = "POSITION";
    input.Format = DXGI_FORMAT_R32G32B32_FLOAT;
    input.InputSlot = 0;
    input.AlignedByteOffset = 0;
    input.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = owner.root.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
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
    desc.InputLayout = {&input, 1};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    hr = owner.device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(owner.pso.GetAddressOf()));
    if (FAILED(hr)) return fail("pso", hr);

    Evidence("device=hardware fence=ready pso=NONE_cull depth=LESS");
    return true;
}

bool ResetForCase(HarnessOwner& owner)
{
    if (!b3a::MayResetList(owner.listState, owner.workState))
    {
        Evidence("reset_rejected list=%d work=%d",
                 static_cast<int>(owner.listState),
                 static_cast<int>(owner.workState));
        return false;
    }
    if (FAILED(owner.allocator->Reset()))
        return false;
    if (FAILED(owner.list->Reset(owner.allocator.Get(), owner.pso.Get())))
        return false;
    owner.listState = b3a::ListStateAfterReset();
    return true;
}

struct GpuCaseOutput
{
    std::vector<uint8_t> greenMask;
    std::vector<uint8_t> nonClearMask;
    std::vector<float> depth;
    float depthMin = 1.0f;
    float depthMax = 0.0f;
    bool greenOnly = true;
    bool clearExact = true;
    size_t nonClear = 0;
    size_t green = 0;
};

void SimulateEmptyReadback(GpuCaseOutput& output)
{
    output.greenMask.assign(kPixelCount, 0);
    output.nonClearMask.assign(kPixelCount, 0);
    output.depth.assign(kPixelCount, 1.0f);
    output.depthMin = 1.0f;
    output.depthMax = 1.0f;
    output.greenOnly = true;
    output.clearExact = true;
    output.nonClear = 0;
    output.green = 0;
}

bool RenderCase(HarnessOwner& owner, const std::vector<ScreenTriangle>& triangles,
                const char* caseName, GpuCaseOutput& output)
{
    owner.lastFenceConfirmed = false;
    if (!ResetForCase(owner))
        return false;
    owner.listState = ListState::Recording;

    // Vertex buffer is submitted work: it must live in the owner before the
    // command list is executed.
    std::vector<float> vertices;
    vertices.reserve(triangles.size() * 9);
    for (const ScreenTriangle& triangle : triangles)
        for (int i = 0; i < 3; ++i)
        {
            vertices.push_back(static_cast<float>(2.0 * triangle.x[i] / kSize - 1.0));
            vertices.push_back(static_cast<float>(1.0 - 2.0 * triangle.y[i] / kSize));
            vertices.push_back(0.5f);
        }

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = vertices.size() * sizeof(float);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> vertexBuffer;
    if (FAILED(owner.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(vertexBuffer.GetAddressOf()))))
        return false;
    void* mapped = nullptr;
    if (FAILED(vertexBuffer->Map(0, nullptr, &mapped)))
        return false;
    std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(float));
    vertexBuffer->Unmap(0, nullptr);
    owner.submittedBuffers.push_back(vertexBuffer);

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(kSize),
                            static_cast<float>(kSize), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(kSize), static_cast<LONG>(kSize)};
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        owner.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        owner.dsvHeap->GetCPUDescriptorHandleForHeapStart();

    owner.list->SetGraphicsRootSignature(owner.root.Get());
    owner.list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    owner.list->ClearRenderTargetView(rtv, kClearColor, 0, nullptr);
    owner.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    owner.list->RSSetViewports(1, &viewport);
    owner.list->RSSetScissorRects(1, &scissor);
    owner.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(float));
    view.StrideInBytes = 3 * sizeof(float);
    owner.list->IASetVertexBuffers(0, 1, &view);
    owner.list->DrawInstanced(static_cast<UINT>(triangles.size() * 3), 1, 0, 0);

    D3D12_RESOURCE_BARRIER toCopy[2] = {};
    toCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy[0].Transition.pResource = owner.color.Get();
    toCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy[1] = toCopy[0];
    toCopy[1].Transition.pResource = owner.depth.Get();
    toCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    owner.list->ResourceBarrier(2, toCopy);

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = owner.color.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = owner.colorReadback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    destination.PlacedFootprint.Footprint.Width = kSize;
    destination.PlacedFootprint.Footprint.Height = kSize;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = kRowPitch;
    owner.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

    D3D12_TEXTURE_COPY_LOCATION depthSource = source;
    depthSource.pResource = owner.depth.Get();
    D3D12_TEXTURE_COPY_LOCATION depthDestination = destination;
    depthDestination.pResource = owner.depthReadback.Get();
    depthDestination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_D32_FLOAT;
    owner.list->CopyTextureRegion(&depthDestination, 0, 0, 0, &depthSource, nullptr);

    // Restore the render states in the same command list so the next case
    // starts from a fully known state and the debug layer sees no invalid
    // StateBefore.
    D3D12_RESOURCE_BARRIER toRender[2] = {};
    toRender[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRender[0].Transition.pResource = owner.color.Get();
    toRender[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toRender[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRender[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRender[1] = toRender[0];
    toRender[1].Transition.pResource = owner.depth.Get();
    toRender[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toRender[1].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    owner.list->ResourceBarrier(2, toRender);

    const HRESULT close = owner.list->Close();
    if (FAILED(close))
    {
        owner.listState = b3a::ListStateAfterFailedClose();
        Evidence("case=%s close_failed=0x%08X", caseName,
                 static_cast<unsigned>(close));
        return false;
    }
    owner.listState = b3a::ListStateAfterClose();

    ID3D12CommandList* lists[] = {owner.list.Get()};
    owner.queue->ExecuteCommandLists(1, lists);
    owner.workState = b3a::StateAfterExecute();
    owner.fenceValue += 1;
    CompletionEvidence completion;
    completion.expected = owner.fenceValue;
    const HRESULT signal = owner.queue->Signal(owner.fence.Get(), owner.fenceValue);
    completion.signalOk = SUCCEEDED(signal);
    if (!completion.signalOk)
        Evidence("case=%s signal_failed=0x%08X", caseName,
                 static_cast<unsigned>(signal));

    bool waitAttempted = false;
    if (completion.signalOk)
    {
        if (owner.fence->GetCompletedValue() < owner.fenceValue)
        {
            waitAttempted = true;
            const HRESULT setEvent =
                owner.fence->SetEventOnCompletion(owner.fenceValue,
                                                  owner.event.get());
            completion.setEventOk = SUCCEEDED(setEvent);
            if (!completion.setEventOk)
                Evidence("case=%s set_event_failed=0x%08X", caseName,
                         static_cast<unsigned>(setEvent));
            if (completion.setEventOk)
            {
                const DWORD wait =
                    ::WaitForSingleObject(owner.event.get(), 10000);
                completion.waitOk = wait == WAIT_OBJECT_0;
                if (!completion.waitOk)
                    Evidence("case=%s wait_failed=%lu", caseName,
                             static_cast<unsigned long>(wait));
            }
        }
    }
    (void)waitAttempted;

    const HRESULT removedHr = owner.device->GetDeviceRemovedReason();
    completion.removedReason = static_cast<long>(removedHr);
    completion.completed = owner.fence->GetCompletedValue();
    completion.getRemovedOk = true;
    owner.workState = ClassifyCompletion(completion);
    if (owner.workState != GpuWorkState::WorkCompleted)
    {
        Evidence("case=%s completion_unconfirmed completed=%llu expected=%llu "
                 "removed=0x%08X state=%s",
                 caseName,
                 static_cast<unsigned long long>(completion.completed),
                 static_cast<unsigned long long>(completion.expected),
                 static_cast<unsigned>(removedHr),
                 owner.workState == GpuWorkState::DeviceRemoved
                     ? "DeviceRemoved"
                     : "WorkSubmittedUnknown");
        TerminalFailure(owner, "completion", kTerminalExit);
    }
    owner.listState = b3a::ListStateAfterConfirmedSubmit();
    owner.lastFenceConfirmed = true;

    // Readback is legal only after confirmed completion.
    void* colorData = nullptr;
    if (FAILED(owner.colorReadback->Map(0, nullptr, &colorData)))
        return false;
    void* depthData = nullptr;
    if (FAILED(owner.depthReadback->Map(0, nullptr, &depthData)))
    {
        owner.colorReadback->Unmap(0, nullptr);
        return false;
    }
    output.greenMask.assign(kPixelCount, 0);
    output.nonClearMask.assign(kPixelCount, 0);
    output.depth.assign(kPixelCount, 1.0f);
    output.depthMin = 1.0f;
    output.depthMax = 0.0f;
    output.greenOnly = true;
    output.clearExact = true;
    output.nonClear = 0;
    output.green = 0;
    for (UINT y = 0; y < kSize; ++y)
    {
        const uint8_t* row = static_cast<const uint8_t*>(colorData) + y * kRowPitch;
        for (UINT x = 0; x < kSize; ++x)
        {
            const uint8_t r = row[x * 4 + 0];
            const uint8_t g = row[x * 4 + 1];
            const uint8_t b = row[x * 4 + 2];
            const size_t index = static_cast<size_t>(y) * kSize + x;
            if (r == 13 && g == 15 && b == 20)
            {
                // Exact clear pixel.
            }
            else
            {
                output.nonClearMask[index] = 1;
                ++output.nonClear;
                if (!(r == 0 && g == 255 && b == 0))
                {
                    output.greenOnly = false;
                    output.clearExact = false;
                }
                else
                {
                    output.greenMask[index] = 1;
                    ++output.green;
                }
            }
        }
    }
    for (UINT y = 0; y < kSize; ++y)
    {
        const float* row = static_cast<const float*>(depthData) + y * (kRowPitch / 4);
        for (UINT x = 0; x < kSize; ++x)
        {
            const float value = row[x];
            output.depth[static_cast<size_t>(y) * kSize + x] = value;
            output.depthMin = std::min(output.depthMin, value);
            output.depthMax = std::max(output.depthMax, value);
        }
    }
    owner.colorReadback->Unmap(0, nullptr);
    owner.depthReadback->Unmap(0, nullptr);
    return true;
}

bool ClassifyAndCheck(CaseId id, const char* caseName, const CpuCaseResult& cpu,
                      const GpuCaseOutput& gpu,
                      const CaseValidation& validation)
{
    ++g_gpuChecks;
    const int failuresBefore = g_gpuFailures;
    const bool validationPass = validation.pass;
    const bool validationUnverified =
        std::strcmp(validation.status, "UNVERIFIED") == 0;
    CheckGpu(validationPass || validationUnverified,
             "per-case InfoQueue validation is clean or explicitly unverified");

    const std::vector<uint8_t> expected = BuildExpectedMask(id);
    const size_t expectedCount = kExpectedCounts[static_cast<int>(id)];
    const size_t cpuCovered = CountMask(cpu.mask);

    Evidence("gpu_case=%s cpu_confidence=%s cpu_pixels=%zu expected_pixels=%zu "
             "gpu_pixels=%zu green_pixels=%zu expected_mask_hash=%016llX "
             "gpu_mask_hash=%016llX depth_min=%.9g depth_max=%.9g",
             caseName, ConfidenceName(cpu.raster.confidence), cpuCovered,
             expectedCount, gpu.nonClear, gpu.green,
             static_cast<unsigned long long>(MaskHash(expected)),
             static_cast<unsigned long long>(MaskHash(gpu.greenMask)),
             gpu.depthMin, gpu.depthMax);

    CheckGpu(gpu.nonClear == expectedCount, "GPU pixel count matches fixed expectation");
    CheckGpu(gpu.green == expectedCount, "GPU green pixel count matches fixed expectation");
    CheckGpu(gpu.nonClearMask == expected, "GPU non-clear mask equals fixed reference mask");
    CheckGpu(gpu.greenMask == expected, "GPU green mask equals fixed reference mask");
    CheckGpu(gpu.greenOnly, "GPU writes only the expected green colour");
    CheckGpu(gpu.clearExact, "every non-covered pixel is exactly the clear colour");

    bool depthOk = true;
    for (size_t index = 0; index < kPixelCount; ++index)
    {
        const bool covered = expected[index] != 0;
        const float depth = gpu.depth[index];
        if (covered)
            depthOk = depthOk && std::isfinite(depth) &&
                      std::fabs(depth - 0.5f) < 1.0e-3f;
        else
            depthOk = depthOk && depth == 1.0f;
    }
    CheckGpu(depthOk, "GPU depth is 0.5 on the fixed mask and 1.0 outside");

    const bool passed = validationPass && g_gpuFailures == failuresBefore;
    PerCase("case=%s run=%d expected=%zu actual=%zu fence=Completed "
            "validation=%s verdict=%s",
            caseName, g_caseRuns[static_cast<int>(id)], expectedCount,
            gpu.nonClear, validation.status, passed ? "PASS" : "FAIL");
    return passed;
}

// ---------------------------------------------------------------------------
// Final acceptance predicate and evidence finalization.
// ---------------------------------------------------------------------------
int PopCount(uint32_t value)
{
    int count = 0;
    while (value != 0)
    {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

int FinalizeHardware(bool gpuAvailable, const char* gpuStatus,
                     const char* acceptance, int exitCode, const char* reason)
{
    bool evidenceOk = true;
    if (!g_outputDirectory.empty())
    {
        auto finish = [&evidenceOk](EvidenceFile& file) {
            const bool flushed = file.Flush();
            const bool closed = file.Close();
            const bool verified = file.Verify();
            const bool stateOk = file.Ok();
            evidenceOk = evidenceOk && flushed && closed && verified && stateOk;
        };
        finish(g_infoQueueDump);
        finish(g_gpuEvidence);
        finish(g_perCaseResults);
    }

    const char* finalAcceptance = acceptance;
    int finalExit = exitCode;
    if (!evidenceOk && std::strcmp(finalAcceptance, "PASS") == 0)
    {
        finalAcceptance = "UNVERIFIED";
        finalExit = kEvidenceUnverifiedExit;
    }
    else if (!evidenceOk && std::strcmp(finalAcceptance, "FAIL") != 0)
    {
        finalAcceptance = "UNVERIFIED";
        finalExit = kEvidenceUnverifiedExit;
    }

    const char* finalGpuStatus = gpuStatus;
    if (std::strcmp(finalAcceptance, "PASS") != 0 &&
        std::strcmp(finalGpuStatus, "PASS") == 0)
        finalGpuStatus = finalAcceptance;

    const int executed = PopCount(g_caseExecutedMask);
    const int completed = PopCount(g_caseCompletedMask);
    bool summaryOk = g_outputDirectory.empty();
    if (!g_outputDirectory.empty())
    {
        g_summary.path = g_outputDirectory + "\\summary.txt";
        if (g_summary.Open(g_summary.path))
        {
            g_summary.WriteF("B3B1_RASTER_ACCEPTANCE_SUMMARY\n");
            g_summary.WriteF("test_mode=HARDWARE\n");
            g_summary.WriteF("gpu_available=%d\n", gpuAvailable ? 1 : 0);
            g_summary.WriteF("gpu_status=%s\n", finalGpuStatus);
            g_summary.WriteF("startup_validation=%s\n", g_startupStatus);
            g_summary.WriteF("gbv_active=%d\n", g_gbvActive ? 1 : 0);
            g_summary.WriteF("mandatory_cases=%d\n", kCaseCount);
            g_summary.WriteF("executed_cases=%d\n", executed);
            g_summary.WriteF("completed_cases=%d\n", completed);
            g_summary.WriteF("duplicate_cases=%d\n", g_duplicateCases);
            g_summary.WriteF("skipped_cases=%d\n", g_skippedCases);
            g_summary.WriteF("cpu_checks=%d\n", g_cpuChecks);
            g_summary.WriteF("cpu_failures=%d\n", g_cpuFailures);
            g_summary.WriteF("gpu_cases=%d\n", g_gpuCases);
            g_summary.WriteF("gpu_checks=%d\n", g_gpuChecks);
            g_summary.WriteF("gpu_failures=%d\n", g_gpuFailures);
            g_summary.WriteF("validation_errors=%d\n", g_validationErrors);
            g_summary.WriteF("validation_unverified=%d\n", g_validationUnverified);
            g_summary.WriteF("evidence_status=%s\n",
                             evidenceOk ? "PASS" : "UNVERIFIED");
            g_summary.WriteF("gpu_execution=%s\n",
                             executed == kCaseCount && g_duplicateCases == 0
                                 ? "COMPLETED"
                                 : "INCOMPLETE");
            g_summary.WriteF("acceptance=%s\n", finalAcceptance);
            g_summary.WriteF("acceptance_reason=%s\n",
                             reason ? reason : "none");
            g_summary.WriteF("exit=%d\n", finalExit);
            const bool flushed = g_summary.Flush();
            summaryOk = g_summary.Close() && flushed;
            summaryOk = g_summary.Verify() && summaryOk;
            summaryOk = g_summary.Ok() && summaryOk;
        }
    }

    if (!g_outputDirectory.empty() && !summaryOk &&
        std::strcmp(finalAcceptance, "PASS") == 0)
    {
        finalAcceptance = "UNVERIFIED";
        finalExit = kEvidenceUnverifiedExit;
    }
    else if (!g_outputDirectory.empty() && !summaryOk &&
             std::strcmp(finalAcceptance, "FAIL") != 0)
    {
        finalAcceptance = "UNVERIFIED";
        finalExit = kEvidenceUnverifiedExit;
    }

    g_evidenceStatus = g_outputDirectory.empty()
                           ? "NOT_WRITTEN"
                           : (evidenceOk && summaryOk ? "PASS" : "UNVERIFIED");
    if (std::strcmp(finalAcceptance, "PASS") != 0 &&
        std::strcmp(finalGpuStatus, "PASS") == 0)
        finalGpuStatus = finalAcceptance;

    std::printf("gpu_available=%d\n", gpuAvailable ? 1 : 0);
    std::printf("gpu_status=%s\n", finalGpuStatus);
    std::printf("startup_validation=%s\n", g_startupStatus);
    std::printf("gbv_active=%d\n", g_gbvActive ? 1 : 0);
    std::printf("gpu_execution=%s\n",
                executed == kCaseCount && g_duplicateCases == 0
                    ? "COMPLETED"
                    : "INCOMPLETE");
    std::printf("evidence_status=%s\n", g_evidenceStatus);
    std::printf("acceptance=%s\n", finalAcceptance);
    std::printf("acceptance_reason=%s\n", reason ? reason : "none");
    std::printf("RASTER_SUMMARY cpu_checks=%d cpu_failures=%d gpu_cases=%d "
                "gpu_checks=%d gpu_failures=%d validation_errors=%d "
                "validation_unverified=%d startup_validation=%s "
                "executed_cases=%d completed_cases=%d duplicates=%d skipped=%d "
                "evidence_status=%s gpu_status=%s acceptance=%s exit=%d\n",
                g_cpuChecks, g_cpuFailures, g_gpuCases, g_gpuChecks,
                g_gpuFailures, g_validationErrors, g_validationUnverified,
                g_startupStatus, executed, completed, g_duplicateCases,
                g_skippedCases, g_evidenceStatus, finalGpuStatus,
                finalAcceptance, finalExit);
    std::fflush(nullptr);
    return finalExit;
}

bool DirectoryExists(const std::string& path)
{
    const DWORD attributes = ::GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::printf("missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (std::strcmp(arg, "--cpu-only") == 0)
            g_cpuOnly = true;
        else if (std::strcmp(arg, "--simulate-no-gpu") == 0)
            g_simulateNoGpu = true;
        else if (std::strcmp(arg, "--output") == 0)
        {
            const char* value = next("--output");
            if (value == nullptr) return 2;
            g_outputDirectory = value;
        }
        else if (std::strcmp(arg, "--fault-startup-error") == 0)
            g_faultStartupError = true;
        else if (std::strcmp(arg, "--fault-startup-corruption") == 0)
            g_faultStartupCorruption = true;
        else if (std::strcmp(arg, "--fault-startup-retrieval") == 0)
            g_faultStartupRetrieval = true;
        else if (std::strcmp(arg, "--fault-no-infoqueue") == 0)
            g_faultNoInfoQueue = true;
        else if (std::strcmp(arg, "--fault-gbv-missing") == 0)
            g_faultGbvMissing = true;
        else if (std::strcmp(arg, "--fault-evidence") == 0)
        {
            const char* value = next("--fault-evidence");
            if (value == nullptr) return 2;
            if (std::strcmp(value, "open") == 0) g_evidenceFault = EvidenceFault::Open;
            else if (std::strcmp(value, "write") == 0) g_evidenceFault = EvidenceFault::Write;
            else if (std::strcmp(value, "flush") == 0) g_evidenceFault = EvidenceFault::Flush;
            else if (std::strcmp(value, "close") == 0) g_evidenceFault = EvidenceFault::Close;
            else if (std::strcmp(value, "missing") == 0) g_evidenceFault = EvidenceFault::Missing;
            else
            {
                std::printf("unknown evidence fault %s\n", value);
                return 2;
            }
        }
        else if (std::strcmp(arg, "--fault-skip-case") == 0)
        {
            const char* value = next("--fault-skip-case");
            if (value == nullptr) return 2;
            if (std::strcmp(value, "all") == 0)
            {
                for (bool& skip : g_faultSkip)
                    skip = true;
            }
            else
            {
                CaseId id = CaseId::Count;
                if (!ParseCaseId(value, id))
                {
                    std::printf("unknown case %s\n", value);
                    return 2;
                }
                g_faultSkip[static_cast<int>(id)] = true;
            }
        }
        else if (std::strcmp(arg, "--fault-duplicate-case") == 0)
        {
            const char* value = next("--fault-duplicate-case");
            if (value == nullptr) return 2;
            CaseId id = CaseId::Count;
            if (!ParseCaseId(value, id))
            {
                std::printf("unknown case %s\n", value);
                return 2;
            }
            g_faultDuplicate[static_cast<int>(id)] = true;
        }
        else if (std::strcmp(arg, "--fault-render-fail") == 0)
        {
            const char* value = next("--fault-render-fail");
            if (value == nullptr) return 2;
            CaseId id = CaseId::Count;
            if (!ParseCaseId(value, id))
            {
                std::printf("unknown case %s\n", value);
                return 2;
            }
            g_faultRenderFail[static_cast<int>(id)] = true;
        }
        else if (std::strcmp(arg, "--fault-empty-readback") == 0)
        {
            const char* value = next("--fault-empty-readback");
            if (value == nullptr) return 2;
            CaseId id = CaseId::Count;
            if (!ParseCaseId(value, id))
            {
                std::printf("unknown case %s\n", value);
                return 2;
            }
            g_faultEmptyReadback[static_cast<int>(id)] = true;
        }
        else if (std::strcmp(arg, "--fault-case-error") == 0)
        {
            const char* value = next("--fault-case-error");
            if (value == nullptr) return 2;
            CaseId id = CaseId::Count;
            if (!ParseCaseId(value, id))
            {
                std::printf("unknown case %s\n", value);
                return 2;
            }
            g_faultCaseError[static_cast<int>(id)] = true;
        }
        else if (std::strcmp(arg, "--fault-case-retrieval") == 0)
        {
            const char* value = next("--fault-case-retrieval");
            if (value == nullptr) return 2;
            CaseId id = CaseId::Count;
            if (!ParseCaseId(value, id))
            {
                std::printf("unknown case %s\n", value);
                return 2;
            }
            g_faultCaseRetrieval[static_cast<int>(id)] = true;
        }
        else
        {
            std::printf("unknown argument %s\n", arg);
            return 2;
        }
    }

    std::printf("B3B1_RASTER_ACCEPTANCE\n");
    std::printf("test_mode=%s\n", g_cpuOnly ? "CPU_ONLY" : "HARDWARE");
    if (g_evidenceFault != EvidenceFault::None)
        std::printf("fault_evidence=%d\n", static_cast<int>(g_evidenceFault));

    RunCpuTests();
    std::printf("cpu_checks=%d cpu_failures=%d\n", g_cpuChecks, g_cpuFailures);
    if (g_cpuOnly)
    {
        std::printf("gpu_status=NOT_RUN_CPU_ONLY\n");
        std::printf("RASTER_SUMMARY cpu_checks=%d cpu_failures=%d gpu_cases=0 "
                    "gpu_status=NOT_RUN_CPU_ONLY acceptance=%s exit=%d\n",
                    g_cpuChecks, g_cpuFailures,
                    g_cpuFailures == 0 ? "CPU_ONLY_PASS" : "FAIL",
                    g_cpuFailures == 0 ? 0 : 1);
        return g_cpuFailures == 0 ? 0 : 1;
    }

    if (!g_simulateNoGpu && g_outputDirectory.empty())
    {
        std::printf("gpu_available=0\ngpu_status=UNVERIFIED\n"
                    "gpu_execution=NOT_STARTED\nacceptance=UNVERIFIED\n"
                    "acceptance_reason=hardware acceptance requires --output\n"
                    "RASTER_SUMMARY cpu_checks=%d cpu_failures=%d gpu_cases=0 "
                    "gpu_status=UNVERIFIED acceptance=UNVERIFIED exit=%d\n",
                    g_cpuChecks, g_cpuFailures, kEvidenceUnverifiedExit);
        return kEvidenceUnverifiedExit;
    }

    if (!g_outputDirectory.empty() && !DirectoryExists(g_outputDirectory))
    {
        std::printf("gpu_available=0\ngpu_status=UNVERIFIED\n"
                    "evidence_status=UNVERIFIED\n"
                    "gpu_execution=NOT_STARTED\nacceptance=UNVERIFIED\n"
                    "acceptance_reason=output directory missing\n"
                    "RASTER_SUMMARY cpu_checks=%d cpu_failures=%d gpu_cases=0 "
                    "gpu_status=UNVERIFIED acceptance=UNVERIFIED exit=%d\n",
                    g_cpuChecks, g_cpuFailures, kEvidenceUnverifiedExit);
        return kEvidenceUnverifiedExit;
    }

    if (!g_outputDirectory.empty())
    {
        g_infoQueueDump.Open(g_outputDirectory + "\\infoqueue-dump.txt");
        g_gpuEvidence.Open(g_outputDirectory + "\\gpu-evidence.txt");
        g_perCaseResults.Open(g_outputDirectory + "\\per-case-results.txt");
        g_perCaseResults.Write("case,run,expected,actual,fence,validation,verdict\n");
    }

    if (g_simulateNoGpu)
    {
        Evidence("gpu_available=0");
        Evidence("gpu_status=UNVERIFIED");
        g_infoQueueDump.Write("infoqueue=simulated_no_gpu\n");
        return FinalizeHardware(false, "UNVERIFIED", "UNVERIFIED",
                                kGpuUnavailableExit, "simulated no GPU");
    }

    HarnessOwner owner;
    if (!InitializeDebugLayer(owner))
    {
        g_infoQueueDump.Write("infoqueue=not_initialized_debug_layer_unavailable\n");
        return FinalizeHardware(false, "UNVERIFIED", "UNVERIFIED",
                                kGpuUnavailableExit, "D3D12 debug layer unavailable");
    }
    if (!InitializeDevice(owner))
    {
        g_infoQueueDump.Write("infoqueue=device_initialization_failed\n");
        return FinalizeHardware(false, "UNVERIFIED", "UNVERIFIED",
                                kGpuUnavailableExit, "GPU initialization failed");
    }
    std::printf("gpu_available=1\ngpu_status=RUNNING\n");

    if (owner.infoQueue)
    {
        if (g_faultStartupError)
            owner.infoQueue->AddApplicationMessage(
                D3D12_MESSAGE_SEVERITY_ERROR,
                "B3B1 injected startup ERROR");
        if (g_faultStartupCorruption)
            owner.infoQueue->AddApplicationMessage(
                D3D12_MESSAGE_SEVERITY_CORRUPTION,
                "B3B1 injected startup CORRUPTION");
    }

    const CaseValidation startup =
        InspectRange(owner, 0, "startup", true, CaseId::A);
    if (owner.infoQueue)
    {
        owner.infoQueue->ClearStoredMessages();
        owner.deniedBaseline =
            owner.infoQueue->GetNumMessagesDeniedByStorageFilter();
        owner.discardedBaseline =
            owner.infoQueue->GetNumMessagesDiscardedByMessageCountLimit();
    }
    if (!owner.infoQueue || !startup.complete || startup.hidden != 0 ||
        startup.discarded != 0)
        g_startupStatus = "UNVERIFIED";
    else if (startup.errors != 0 || startup.corruptions != 0)
        g_startupStatus = "FAIL";
    else
        g_startupStatus = "PASS";
    g_gbvActive = owner.gbvActive;
    Evidence("startup_validation=%s gbv_active=%d", g_startupStatus,
             g_gbvActive ? 1 : 0);

    auto terminalAction = [&owner]() noexcept {
        TerminalFailure(owner, "guard", kTerminalExit);
    };
    b3a::UnknownCompletionGuard guard(owner.workState, terminalAction);
    (void)guard;

    std::vector<CaseId> plan = {CaseId::A, CaseId::B, CaseId::C, CaseId::D,
                                CaseId::EMinus, CaseId::EZero, CaseId::EPlus,
                                CaseId::F};
    for (int i = 0; i < kCaseCount; ++i)
    {
        if (g_faultDuplicate[i])
        {
            auto position = std::find(plan.begin(), plan.end(),
                                      static_cast<CaseId>(i));
            if (position != plan.end())
                plan.insert(position + 1, static_cast<CaseId>(i));
        }
    }

    for (CaseId id : plan)
    {
        const int caseIndex = static_cast<int>(id);
        const char* name = CaseName(id);
        ++g_caseRuns[caseIndex];
        if (g_faultSkip[caseIndex])
        {
            ++g_skippedCases;
            PerCase("case=%s run=%d expected=%zu actual=0 fence=NOT_RUN "
                    "validation=NOT_RUN verdict=SKIPPED",
                    name, g_caseRuns[caseIndex],
                    kExpectedCounts[caseIndex]);
            continue;
        }

        GpuCaseOutput output;
        bool rendered = true;
        if (g_faultRenderFail[caseIndex])
        {
            Evidence("case=%s render_failed_by_fault", name);
            rendered = false;
        }
        else
        {
            const std::vector<ScreenTriangle> triangles = [&]() {
                switch (id)
                {
                case CaseId::A:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(0, 0, 1, 0, 0, 1)};
                case CaseId::B:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(10, 10, 20, 10, 10, 20)};
                case CaseId::C:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(0, 0, 0, 1, 1, 0)};
                case CaseId::D:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(10, 10, 20, 10, 10, 20),
                        MakeTriangle(20, 10, 20, 20, 10, 20)};
                case CaseId::EMinus:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(0, 0, 1.0 - 1.0 / 256.0, 0,
                                     0, 1.0 - 1.0 / 256.0)};
                case CaseId::EZero:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(0, 0, 1.0, 0, 0, 1.0)};
                case CaseId::EPlus:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(0, 0, 1.0 + 1.0 / 256.0, 0,
                                     0, 1.0 + 1.0 / 256.0)};
                case CaseId::F:
                    return std::vector<ScreenTriangle>{
                        MakeTriangle(-40, -40, 100, -40, -40, 100)};
                default:
                    return std::vector<ScreenTriangle>{};
                }
            }();
            rendered = RenderCase(owner, triangles, name, output);
        }
        if (!rendered)
        {
            Evidence("case=%s render_failed", name);
            ++g_gpuFailures;
            PerCase("case=%s run=%d expected=%zu actual=0 fence=NOT_CONFIRMED "
                    "validation=NOT_RUN verdict=FAIL",
                    name, g_caseRuns[caseIndex], kExpectedCounts[caseIndex]);
            if (b3a::MustRetainUntilCompletion(owner.workState))
                TerminalFailure(owner, "render", kTerminalExit);
            break;
        }

        g_caseExecutedMask |= 1u << caseIndex;
        ++g_gpuCases;
        if (owner.lastFenceConfirmed)
            g_caseFenceMask |= 1u << caseIndex;

        if (g_faultCaseError[caseIndex] && owner.infoQueue)
            owner.infoQueue->AddApplicationMessage(
                D3D12_MESSAGE_SEVERITY_ERROR,
                "B3B1 injected per-case ERROR");
        const CaseValidation validation =
            InspectRange(owner, 0, name, false, id);
        if (std::strcmp(validation.status, "FAIL") == 0)
            ++g_validationErrors;
        else if (std::strcmp(validation.status, "UNVERIFIED") == 0)
            ++g_validationUnverified;
        if (validation.pass)
            g_caseValidationMask |= 1u << caseIndex;
        if (owner.infoQueue)
            owner.infoQueue->ClearStoredMessages();

        if (g_faultEmptyReadback[caseIndex])
        {
            Evidence("case=%s empty_readback_fault_injected", name);
            SimulateEmptyReadback(output);
        }

        const CpuCaseResult cpu = RunCpu([&]() {
            switch (id)
            {
            case CaseId::A:
                return std::vector<ScreenTriangle>{MakeTriangle(0, 0, 1, 0, 0, 1)};
            case CaseId::B:
                return std::vector<ScreenTriangle>{MakeTriangle(10, 10, 20, 10, 10, 20)};
            case CaseId::C:
                return std::vector<ScreenTriangle>{MakeTriangle(0, 0, 0, 1, 1, 0)};
            case CaseId::D:
                return std::vector<ScreenTriangle>{
                    MakeTriangle(10, 10, 20, 10, 10, 20),
                    MakeTriangle(20, 10, 20, 20, 10, 20)};
            case CaseId::EMinus:
                return std::vector<ScreenTriangle>{
                    MakeTriangle(0, 0, 1.0 - 1.0 / 256.0, 0,
                                 0, 1.0 - 1.0 / 256.0)};
            case CaseId::EZero:
                return std::vector<ScreenTriangle>{MakeTriangle(0, 0, 1.0, 0, 0, 1.0)};
            case CaseId::EPlus:
                return std::vector<ScreenTriangle>{
                    MakeTriangle(0, 0, 1.0 + 1.0 / 256.0, 0,
                                 0, 1.0 + 1.0 / 256.0)};
            case CaseId::F:
                return std::vector<ScreenTriangle>{
                    MakeTriangle(-40, -40, 100, -40, -40, 100)};
            default:
                return std::vector<ScreenTriangle>{};
            }
        }());
        const bool casePass =
            ClassifyAndCheck(id, name, cpu, output, validation);
        if (casePass)
            g_caseCompletedMask |= 1u << caseIndex;
    }

    g_gbvActive = owner.gbvActive;
    const int executedCount = PopCount(g_caseExecutedMask);
    const int completedCount = PopCount(g_caseCompletedMask);
    for (int runs : g_caseRuns)
        if (runs > 1)
            ++g_duplicateCases;

    bool mandatoryComplete = g_caseExecutedMask == kAllCasesMask &&
                             g_caseCompletedMask == kAllCasesMask &&
                             g_caseFenceMask == kAllCasesMask &&
                             g_duplicateCases == 0 && g_skippedCases == 0 &&
                             g_gpuCases == kCaseCount;
    const bool startupFail = std::strcmp(g_startupStatus, "FAIL") == 0;
    const bool startupUnverified =
        std::strcmp(g_startupStatus, "PASS") != 0;

    const char* acceptance = "PASS";
    int exitCode = 0;
    const char* reason = "all mandatory GPU cases passed";
    if (g_cpuFailures != 0 || g_gpuFailures != 0 || g_validationErrors != 0 ||
        startupFail)
    {
        acceptance = "FAIL";
        exitCode = 1;
        reason = "CPU/GPU/validation/startup failure";
    }
    else if (!owner.infoQueue || !owner.debugLayerAvailable || !owner.gbvActive ||
             startupUnverified || g_validationUnverified != 0)
    {
        acceptance = "UNVERIFIED";
        exitCode = kEvidenceUnverifiedExit;
        reason = "validation or GBV evidence unavailable";
    }
    else if (!mandatoryComplete)
    {
        acceptance = "FAIL";
        exitCode = 1;
        reason = "mandatory GPU cases were skipped, duplicated or incomplete";
    }

    std::printf("gpu_cases=%d gpu_checks=%d gpu_failures=%d validation_errors=%d "
                "validation_unverified=%d executed=%d completed=%d "
                "duplicates=%d skipped=%d\n",
                g_gpuCases, g_gpuChecks, g_gpuFailures, g_validationErrors,
                g_validationUnverified, executedCount, completedCount,
                g_duplicateCases, g_skippedCases);
    return FinalizeHardware(true, acceptance, acceptance, exitCode, reason);
}
