#pragma once

#include "dx12geometrycatalog.h"
#include "dx12objectconstants.h"
#include "dx12quarantine.h"
#include "dx12xrayasset.h"

#include <array>
#include <vector>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// F3.5.6B: render-owned GPU geometry registry. The catalogue is immutable
// metadata; the registry owns the actual VB/IB/upload buffers and the single
// indexed pipeline used by the geometry scene. Owner thread creates and
// destroys it; workers only consume immutable GpuDraw12 records.
class GpuMeshRegistry12
{
public:
    static constexpr uint32_t kMaxMeshes = GeometryCatalog12::kMaxMeshes;

    GpuMeshRegistry12() = default;
    ~GpuMeshRegistry12();

    GpuMeshRegistry12(const GpuMeshRegistry12&) = delete;
    GpuMeshRegistry12& operator=(const GpuMeshRegistry12&) = delete;

    bool Create(ID3D12Device* device,
                ID3D12CommandQueue* copyQueue,
                ID3D12CommandAllocator* copyAllocator,
                ID3D12GraphicsCommandList* copyCommandList);

    // F3.6A: one fixed X-Ray asset slot. The texture pointer may be null to
    // request the explicit 1x1 fallback; the real asset path must pass a
    // validated BC1 texture.
    bool CreateXrayAsset(ID3D12Device* device,
                         ID3D12CommandQueue* copyQueue,
                         ID3D12CommandAllocator* copyAllocator,
                         ID3D12GraphicsCommandList* copyCommandList,
                         const XrayAssetData12& asset,
                         const XrayTextureData12* texture);

    bool BuildXrayScene(const XrayAssetData12& asset,
                        std::vector<renderprep12::Object12>& objects) const;

    bool AssetTextureFallback() const { return m_assetTextureFallback; }
    const D3D12_VERTEX_BUFFER_VIEW& AssetVertexView() const { return m_assetVertexView; }
    const D3D12_INDEX_BUFFER_VIEW& AssetIndexView() const { return m_assetIndexView; }
    uint32_t AssetIndexCount() const { return m_assetIndexCount; }

    void OnUploadComplete();
    void Release();
    void Quarantine(GpuQuarantine& quarantine);

    bool IsCreated() const { return m_created; }
    const GeometryCatalog12& Catalog() const { return m_catalog; }

    // Owner-side representative scene: 49 objects reusing three geometry
    // handles with deterministic transforms and indexed submesh ranges.
    bool BuildScene(std::vector<renderprep12::Object12>& objects) const;

    // Recording worker path. All referenced views and ranges came from the
    // immutable resolved frame; this function does not touch the registry
    // catalogue or any mutable renderer state.
    bool RecordDraw(ID3D12GraphicsCommandList* list,
                    const GpuDraw12& draw,
                    D3D12_GPU_VIRTUAL_ADDRESS frameCBVA,
                    D3D12_GPU_VIRTUAL_ADDRESS objectCBVA) const;

private:
    struct MeshGpu
    {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;
        ComPtr<ID3D12Resource> uploadVertex;
        ComPtr<ID3D12Resource> uploadIndex;
    };

    struct AssetGpu
    {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;
        ComPtr<ID3D12Resource> uploadVertex;
        ComPtr<ID3D12Resource> uploadIndex;
    };

    bool CreatePipeline(ID3D12Device* device);
    bool CreateMeshResources(ID3D12Device* device,
                             ID3D12CommandAllocator* allocator,
                             ID3D12GraphicsCommandList* list);
    bool CreateTexturedPipeline(ID3D12Device* device);
    bool CreateXrayResources(ID3D12Device* device,
                             ID3D12CommandAllocator* allocator,
                             ID3D12GraphicsCommandList* list,
                             const XrayAssetData12& asset,
                             const XrayTextureData12* texture);

    GeometryCatalog12 m_catalog;
    std::array<MeshGpu, kMaxMeshes> m_meshes{};
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    bool m_copySubmitted = false;
    bool m_created = false;

    AssetGpu m_asset{};
    ComPtr<ID3D12Resource> m_assetTexture;
    ComPtr<ID3D12Resource> m_assetTextureUpload;
    ComPtr<ID3D12DescriptorHeap> m_assetSrvHeap;
    ComPtr<ID3D12RootSignature> m_assetRootSignature;
    ComPtr<ID3D12PipelineState> m_assetPipelineState;
    D3D12_VERTEX_BUFFER_VIEW m_assetVertexView{};
    D3D12_INDEX_BUFFER_VIEW m_assetIndexView{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_assetSrvGpu{};
    uint32_t m_assetIndexCount = 0;
    bool m_assetCreated = false;
    bool m_assetTextureFallback = false;
};

static_assert(GpuMeshRegistry12::kMaxMeshes == GpuQuarantine::kRegistryMeshCount,
              "geometry registry and quarantine capacities must match");
