#pragma once

#include "Common.h"

class Renderer;

// Fullscreen composite: sky * transmittance + in-scattered cloud radiance.
// The pass is intentionally small and has no dependency on Win32 or the noise
// generator.
class CloudCompositor
{
public:
    CloudCompositor();
    ~CloudCompositor();

    CloudCompositor(const CloudCompositor&) = delete;
    CloudCompositor& operator=(const CloudCompositor&) = delete;

    void Initialize(Renderer& renderer);
    void Shutdown();

    void Draw(
        ID3D12GraphicsCommandList* list,
        D3D12_GPU_VIRTUAL_ADDRESS constants,
        D3D12_GPU_DESCRIPTOR_HANDLE cloudSrv,
        UINT width,
        UINT height);

private:
    Renderer* renderer_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
};
