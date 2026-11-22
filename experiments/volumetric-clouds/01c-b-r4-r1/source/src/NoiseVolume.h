#pragma once

#include "Common.h"
#include "NoiseTypes.h"

#include <cstdint>
#include <vector>

class Renderer;

// Owns the 3D noise texture, its UAV/SRV descriptors, and the compute pipeline.
// It has no dependency on the window, input, or the slice viewer; V1 can reuse
// this class unchanged to feed a ray-marching pass.
class NoiseVolume
{
public:
    static constexpr UINT kVolumeUavSlot = 0;
    static constexpr UINT kVolumeSrvSlot = 1;
    static constexpr UINT kProbeUavSlot = 2;

    struct PeriodicityProbe
    {
        float maxDifference[3]{}; // X, Y, Z
        float negativeControl = 0.0f;
    };

    struct SamplerWrapProbe
    {
        float maxFilterError = 0.0f;      // GPU sample vs CPU trilinear WRAP
        float maxWrapDifference = 0.0f;   // p vs p + unit axis
        float maxClampDifference = 0.0f;  // proves the seam test is non-trivial
    };

    NoiseVolume();
    ~NoiseVolume();

    NoiseVolume(const NoiseVolume&) = delete;
    NoiseVolume& operator=(const NoiseVolume&) = delete;

    void Initialize(Renderer& renderer, const NoiseParams& params,
                    DXGI_FORMAT preferredFormat = DXGI_FORMAT_UNKNOWN);
    void Shutdown();

    // Recreates the texture at a new resolution. The caller must invoke
    // Generate() afterwards; descriptors stay at the same heap slots.
    void ResizeVolume(uint32_t resolution);

    void Generate(const NoiseParams& params);
    std::vector<float> ReadbackRgba();
    PeriodicityProbe ProbePeriodicity();
    SamplerWrapProbe ProbeSamplerWrap(const std::vector<float>& rgba);

    bool IsInitialized() const { return initialized_; }
    uint32_t Resolution() const { return params_.resolution; }
    const NoiseParams& Params() const { return params_; }
    DXGI_FORMAT Format() const { return format_; }
    const char* FormatName() const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle() const;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle() const;

private:
    void SelectFormat(DXGI_FORMAT preferredFormat);
    void CreatePipeline();
    void CreateResource(uint32_t resolution);
    void CreateProbeBuffer();
    void WriteDescriptors();

    Renderer* renderer_ = nullptr;
    NoiseParams params_{};
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bool initialized_ = false;

    ComPtr<ID3D12Resource> volume_;
    ComPtr<ID3D12Resource> probeBuffer_;
    D3D12_RESOURCE_STATES probeState_ = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> computePso_;
    ComPtr<ID3D12PipelineState> probePso_;
    ComPtr<ID3D12PipelineState> samplerProbePso_;
};
