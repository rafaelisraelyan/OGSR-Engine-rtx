#pragma once

#include "dx12objectconstants.h"
#include "dx12renderprep.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>

// F3.5.6B: render-owned indexed geometry catalogue. This layer contains only
// immutable descriptors and owner-side resolution; GPU resource ownership is
// in GpuMeshRegistry12.
struct MeshSubmesh12
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t baseVertex = 0;
};

struct MeshDesc12
{
    renderprep12::Resource12 handle;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    MeshSubmesh12 submesh;
    DirectX::XMFLOAT3 minimum{};
    DirectX::XMFLOAT3 maximum{};
    renderprep12::Sphere12 sphere{};
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
};

class GeometryCatalog12
{
public:
    static constexpr uint32_t kMaxMeshes = 3;

    GeometryCatalog12();

    uint32_t Count() const { return m_count; }
    const MeshDesc12* Find(renderprep12::Resource12 handle) const;
    int32_t IndexOf(renderprep12::Resource12 handle) const;
    const MeshDesc12& At(uint32_t index) const { return m_meshes[index]; }

    // Owner-only update after VB/IB upload. Views are copied into
    // ResolvedFrame12, so workers never dereference GPU resources.
    void SetViews(uint32_t index,
                  const D3D12_VERTEX_BUFFER_VIEW& vertexView,
                  const D3D12_INDEX_BUFFER_VIEW& indexView);

private:
    std::array<MeshDesc12, kMaxMeshes> m_meshes{};
    uint32_t m_count = 0;
};

struct GpuDraw12
{
    uint32_t meshIndex = 0;
    // 0 = existing synthetic/geometry pipeline, 1 = F3.6A textured X-Ray
    // pipeline. This is immutable resolved-frame data, never a worker lookup.
    uint32_t pipeline = 0;
    uint32_t objectConstantIndex = 0;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t baseVertex = 0;
};

struct ResolvedFrame12
{
    uint64_t generation = 0;
    std::vector<GpuDraw12> draws;
};

// Owner-side validation and resolution. Unknown/stale handles, invalid
// submesh ranges and capacity overflow are rejected before Reset/Execute.
bool ResolveGeometryFrame(const renderprep12::OrderedFrame12& frame,
                          uint64_t liveGeneration,
                          const GeometryCatalog12& catalog,
                          ResolvedFrame12& resolved,
                          const char** reason);

bool Equivalent(const ResolvedFrame12& a, const ResolvedFrame12& b);

// Validate every index in mesh data, including signed baseVertex. Runs on the
// CPU before VB/IB creation and is also exercised by CPU-only tests.
bool ValidateIndexData(const MeshDesc12& mesh,
                       const uint32_t* indices,
                       size_t count,
                       const char** reason);

// CPU-only tests for catalogue lookup, ranges and deterministic resolution.
bool RunGeometryTests();
