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
};

// Compute ray-marching pass. It owns three half-resolution cloud textures, one
// per back buffer, plus their stable UAV/SRV descriptors and upload constants.
class CloudRayMarcher
{
public:
    static constexpr UINT kFrameCount = 3;
    static constexpr UINT kUavSlotBase = 3;
    static constexpr UINT kSrvSlotBase = 6;

    CloudRayMarcher();
    ~CloudRayMarcher();

    CloudRayMarcher(const CloudRayMarcher&) = delete;
    CloudRayMarcher& operator=(const CloudRayMarcher&) = delete;

    void Initialize(Renderer& renderer, uint32_t swapWidth, uint32_t swapHeight);
    void Shutdown();
    void Resize(uint32_t swapWidth, uint32_t swapHeight);

    void Render(
        ID3D12GraphicsCommandList* list,
        uint32_t frameIndex,
        const clouds::CloudFrameConstants& constants,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv);

    // Synchronous readback used only by the selftest. It waits for the GPU and
    // uses frame 0's texture, then restores it to ALL_SHADER_RESOURCE.
    CloudReadback Readback(
        const clouds::CloudFrameConstants& constants,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv);

    D3D12_GPU_VIRTUAL_ADDRESS ConstantsAddress(uint32_t frameIndex) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle(uint32_t frameIndex) const;
    UINT OutputWidth() const { return outputWidth_; }
    UINT OutputHeight() const { return outputHeight_; }
    DXGI_FORMAT Format() const { return format_; }
    const char* FormatName() const;

private:
    void SelectFormat();
    void CreatePipeline();
    void CreateTextures();
    void CreateConstants();
    void UploadConstants(uint32_t frameIndex,
                         const clouds::CloudFrameConstants& constants);
    void Dispatch(
        ID3D12GraphicsCommandList* list,
        uint32_t frameIndex,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv);
    void TransitionCloud(
        ID3D12GraphicsCommandList* list,
        uint32_t frameIndex,
        D3D12_RESOURCE_STATES after);

    Renderer* renderer_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    ComPtr<ID3D12Resource> textures_[kFrameCount];
    D3D12_RESOURCE_STATES states_[kFrameCount]{};
    ComPtr<ID3D12Resource> constantBuffers_[kFrameCount];
    uint8_t* mappedConstants_[kFrameCount]{};

    uint32_t swapWidth_ = 0;
    uint32_t swapHeight_ = 0;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    bool initialized_ = false;
};
