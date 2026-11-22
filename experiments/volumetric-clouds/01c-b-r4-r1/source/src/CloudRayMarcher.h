#pragma once

#include "CloudTypes.h"
#include "Common.h"

#include <cstdint>
#include <string>
#include <vector>

class Renderer;

struct CloudReadback
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> rgba; // tightly packed RGBA32F
    clouds::CloudFormula formula = clouds::CloudFormula::None;
    clouds::CloudDensityModel densityModel = clouds::CloudDensityModel::Legacy;
    clouds::ReferenceStage referenceStage = clouds::ReferenceStage::None;
    std::string shaderVariant;
    std::string renderPso;
    std::string probePso;
};

struct CloudProbeReadback
{
    uint32_t rows = 0;
    uint32_t steps = 0;
    uint32_t records = 3;
    std::vector<float> values; // float32, row-major (row, step, record, 4)
};

// Compute ray-marching pass. It owns three half-resolution cloud textures, one
// per back buffer, plus their stable UAV/SRV descriptors and upload constants.
class CloudRayMarcher
{
public:
    static constexpr UINT kFrameCount = 3;
    static constexpr UINT kUavSlotBase = 3;
    static constexpr UINT kSrvSlotBase = 6;
    static constexpr UINT kProbeUavSlot = 12;
    static constexpr UINT kProbeMaxSteps = 256;

    CloudRayMarcher();
    ~CloudRayMarcher();

    CloudRayMarcher(const CloudRayMarcher&) = delete;
    CloudRayMarcher& operator=(const CloudRayMarcher&) = delete;

    void Initialize(Renderer& renderer, uint32_t swapWidth, uint32_t swapHeight,
                    uint32_t outputDivisor = 2);
    void Shutdown();
    void Resize(uint32_t swapWidth, uint32_t swapHeight);
    // V-WEATHER-01C-B: GPU-safe public quality divisor change. Waits for the
    // full GPU fence before retiring and recreating cloud output textures and
    // descriptors. Returns the new resource generation.
    uint64_t SetOutputDivisor(uint32_t outputDivisor);

    void Render(
        ID3D12GraphicsCommandList* list,
        uint32_t frameIndex,
        const clouds::CloudFrameConstants& constants,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
        clouds::ShapePrototype prototype,
        clouds::CloudFormula formula,
        clouds::CloudDensityModel densityModel);

    // Synchronous readback used only by the selftest. It waits for the GPU and
    // uses frame 0's texture, then restores it to ALL_SHADER_RESOURCE.
    CloudReadback Readback(
        const clouds::CloudFrameConstants& constants,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
        clouds::CloudFormula formula = clouds::CloudFormula::None,
        clouds::CloudDensityModel densityModel = clouds::CloudDensityModel::Legacy,
        clouds::ReferenceStage referenceStage = clouds::ReferenceStage::Height);

    // Synchronous float32 readback of the ray-profile probe buffer. Call only
    // after a ray-profile frame has completed (capture path).
    CloudProbeReadback ReadbackProbe(uint32_t rows, uint32_t steps);

    // A02c: copy N float4 records from the probe buffer without re-dispatch.
    // Used for the per-pixel optical-depth map and the boundary probe.
    std::vector<float> ReadbackRawRecords(uint32_t count);

    // A02c: dispatch the reference density volume (mode 0), the boundary
    // column probe (mode 1) or the packed density columns (mode 2) and return
    // its float4 records. Grid parameters must already be in
    // constants.referenceVolume / referenceGrid / referenceAudit.
    std::vector<float> ReadbackDensityVolume(
        const clouds::CloudFrameConstants& constants,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
        uint32_t gridX, uint32_t gridY, uint32_t gridZ, uint32_t mode);

    D3D12_GPU_VIRTUAL_ADDRESS ConstantsAddress(uint32_t frameIndex) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle(uint32_t frameIndex) const;
    UINT OutputWidth() const { return outputWidth_; }
    UINT OutputHeight() const { return outputHeight_; }
    uint32_t OutputDivisor() const { return outputDivisor_; }
    uint64_t ResourceGeneration() const { return resourceGeneration_; }
    bool IsInitialized() const { return initialized_; }
    DXGI_FORMAT Format() const { return format_; }
    const char* FormatName() const;

private:
    void SelectFormat();
    void CreatePipeline();
    void RecreateOutputResources(uint32_t width, uint32_t height,
                                 uint32_t divisor, const char* reason,
                                 uint32_t probeRecords = 0);
    void CreateTextureCandidates(
        uint32_t width, uint32_t height,
        ComPtr<ID3D12Resource> (&candidates)[kFrameCount]);
    void CreateProbeCandidate(uint32_t width, uint32_t height,
                              uint32_t records,
                              ComPtr<ID3D12Resource>& candidate);
    void PublishTextureCandidates(
        ComPtr<ID3D12Resource> (&candidates)[kFrameCount]);
    void PublishProbeCandidate(ComPtr<ID3D12Resource> candidate,
                               uint32_t rows, uint32_t records);
    void CreateConstants();
    void EnsureProbeCapacity(uint32_t records);
    void UploadConstants(uint32_t frameIndex,
                         const clouds::CloudFrameConstants& constants);
    void DispatchVolume(
        ID3D12GraphicsCommandList* list,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
        uint32_t gridX, uint32_t gridY, uint32_t gridZ, uint32_t mode,
        bool a03a, bool a03b);
    void Dispatch(
        ID3D12GraphicsCommandList* list,
        uint32_t frameIndex,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
        bool probe,
        bool transition,
        bool shadowOd,
        bool shadowInner,
        clouds::ShapePrototype prototype,
        clouds::CloudFormula formula,
        clouds::CloudDensityModel densityModel,
        bool a03a, bool a03b);
    void TransitionCloud(
        ID3D12GraphicsCommandList* list,
        uint32_t frameIndex,
        D3D12_RESOURCE_STATES after);

    Renderer* renderer_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    ComPtr<ID3D12PipelineState> pipelineStateTransition_;
    ComPtr<ID3D12PipelineState> pipelineStateTransitionShadowOD_;
    ComPtr<ID3D12PipelineState> pipelineStateTransitionShadowInner_;
    ComPtr<ID3D12PipelineState> pipelineStateProbe_;
    ComPtr<ID3D12PipelineState> pipelineStateProbeFormula_[5];
    ComPtr<ID3D12PipelineState> pipelineStateProbeProtoA_;
    ComPtr<ID3D12PipelineState> pipelineStateProbeProtoB_;
    ComPtr<ID3D12PipelineState> pipelineStateReference_;
    ComPtr<ID3D12PipelineState> pipelineStateReferenceA03a_;
    ComPtr<ID3D12PipelineState> pipelineStateReferenceA03b_;
    ComPtr<ID3D12PipelineState> pipelineStateProbeReference_;
    ComPtr<ID3D12PipelineState> pipelineStateProbeReferenceA03a_;
    ComPtr<ID3D12PipelineState> pipelineStateProbeReferenceA03b_;
    ComPtr<ID3D12PipelineState> pipelineStateDensityVolume_;
    ComPtr<ID3D12PipelineState> pipelineStateDensityVolumeA03a_;
    ComPtr<ID3D12PipelineState> pipelineStateStormBoundaryA03a_;
    ComPtr<ID3D12PipelineState> pipelineStateDensityColumnsA03a_;
    ComPtr<ID3D12PipelineState> pipelineStateDensityVolumeA03b_;
    ComPtr<ID3D12PipelineState> pipelineStateStormBoundaryA03b_;
    ComPtr<ID3D12PipelineState> pipelineStateDensityColumnsA03b_;
    ComPtr<ID3D12PipelineState> pipelineStateStormBoundary_;
    ComPtr<ID3D12PipelineState> pipelineStateDensityColumns_;
    ID3D12PipelineState* lastLoggedProbePipeline_ = nullptr;
    ID3D12PipelineState* lastLoggedRenderPipeline_ = nullptr;
    ComPtr<ID3D12PipelineState> pipelineStateProtoA_;
    ComPtr<ID3D12PipelineState> pipelineStateProtoB_;
    ComPtr<ID3D12PipelineState> pipelineStateFormula_[5];
    ComPtr<ID3D12Resource> textures_[kFrameCount];
    D3D12_RESOURCE_STATES states_[kFrameCount]{};
    ComPtr<ID3D12Resource> constantBuffers_[kFrameCount];
    uint8_t* mappedConstants_[kFrameCount]{};
    ComPtr<ID3D12Resource> probeBuffer_;
    uint32_t probeCapacityRows_ = 0;
    uint32_t probeCapacityRecords_ = 0;

    uint32_t swapWidth_ = 0;
    uint32_t swapHeight_ = 0;
    uint32_t outputDivisor_ = 2;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    uint64_t resourceGeneration_ = 0;
    bool initialized_ = false;
};
