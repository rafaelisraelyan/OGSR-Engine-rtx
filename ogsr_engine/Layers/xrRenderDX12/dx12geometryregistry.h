#pragma once

#include "dx12geometrycatalog.h"
#include "dx12objectconstants.h"
#include "dx12quarantine.h"

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

    bool CreatePipeline(ID3D12Device* device);
    bool CreateMeshResources(ID3D12Device* device,
                             ID3D12CommandAllocator* allocator,
                             ID3D12GraphicsCommandList* list);

    GeometryCatalog12 m_catalog;
    std::array<MeshGpu, kMaxMeshes> m_meshes{};
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    bool m_copySubmitted = false;
    bool m_created = false;
};

static_assert(GpuMeshRegistry12::kMaxMeshes == GpuQuarantine::kRegistryMeshCount,
              "geometry registry and quarantine capacities must match");
