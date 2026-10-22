#pragma once

#include "Common.h"
#include "NoiseTypes.h"

class Renderer;

// Fullscreen slice renderer. It consumes a Texture3D SRV and draws one triangle;
// it does not know how that texture was generated.
class SliceViewer
{
public:
    SliceViewer();
    ~SliceViewer();

    SliceViewer(const SliceViewer&) = delete;
    SliceViewer& operator=(const SliceViewer&) = delete;

    void Initialize(Renderer& renderer);
    void Shutdown();

    void Draw(
        ID3D12GraphicsCommandList* list,
        UINT width,
        UINT height,
        const ViewConstants& constants,
        D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv);

private:
    Renderer* renderer_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
};
