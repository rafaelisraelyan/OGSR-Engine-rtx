#pragma once

#include "Common.h"

#include <cstdint>
#include <vector>

class Renderer;

// Low-frequency world-space weather map. The map is static during normal
// rendering and is regenerated only on an explicit seed/update request. It is
// deliberately independent of the swap-chain size: resizing the window never
// invalidates its SRV.
class WeatherMap
{
public:
    static constexpr UINT kResolution = 256;
    static constexpr UINT kUavSlot = 9;
    static constexpr UINT kSrvSlot = 10;
    static constexpr UINT kProbeUavSlot = 11;

    struct SamplerWrapProbe
    {
        float maxWrapDifference = 0.0f;
        float maxRgbaDifference = 0.0f;
    };

    WeatherMap();
    ~WeatherMap();

    WeatherMap(const WeatherMap&) = delete;
    WeatherMap& operator=(const WeatherMap&) = delete;

    void Initialize(Renderer& renderer, uint32_t seed);
    void Generate(uint32_t seed);
    void Shutdown();

    // Synchronous readback used by the selftest and evidence tooling. It waits
    // for the GPU, copies the map, and restores ALL_SHADER_RESOURCE.
    std::vector<float> ReadbackRgba();
    SamplerWrapProbe ProbeSamplerWrap();

    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle() const;
    uint32_t Resolution() const { return kResolution; }
    uint32_t Seed() const { return seed_; }
    double LastGenerateMs() const { return lastGenerateMs_; }
    DXGI_FORMAT Format() const { return format_; }
    const char* FormatName() const;

private:
    void SelectFormat();
    void CreateResource();
    void CreateDescriptors();
    void CreatePipeline();
    void CreateProbeBuffer();
    void Transition(ID3D12GraphicsCommandList* list,
                    D3D12_RESOURCE_STATES after);

    Renderer* renderer_ = nullptr;
    ComPtr<ID3D12Resource> texture_;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    ComPtr<ID3D12RootSignature> probeRootSignature_;
    ComPtr<ID3D12PipelineState> probePipelineState_;
    ComPtr<ID3D12Resource> probeBuffer_;
    D3D12_RESOURCE_STATES probeState_ = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    uint32_t seed_ = 0;
    double lastGenerateMs_ = 0.0;
    bool initialized_ = false;
};
