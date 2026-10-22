#include "dx12geometrycatalog.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace
{
using renderprep12::Resource12;
using renderprep12::Sphere12;

MeshDesc12 MakeMesh(Resource12 handle, uint32_t vertexCount,
                    uint32_t indexCount, const DirectX::XMFLOAT3& minimum,
                    const DirectX::XMFLOAT3& maximum,
                    const DirectX::XMFLOAT3& sphereCenter, float sphereRadius)
{
    MeshDesc12 mesh{};
    mesh.handle = handle;
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;
    mesh.submesh.firstIndex = 0;
    mesh.submesh.indexCount = indexCount;
    mesh.submesh.baseVertex = 0;
    mesh.minimum = minimum;
    mesh.maximum = maximum;
    mesh.sphere.center = sphereCenter;
    mesh.sphere.radius = sphereRadius;
    return mesh;
}
} // namespace

GeometryCatalog12::GeometryCatalog12()
{
    // IDs are separate from the legacy synthetic table; generation 1 is the
    // initial render-owned lifetime.
    const Resource12 cube{1, 1};
    const Resource12 plane{2, 3};
    const Resource12 pyramid{3, 2};

    m_meshes[0] = MakeMesh(cube, 8, 36, {-0.5f, -0.5f, -0.5f},
                           {0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f},
                           std::sqrt(0.75f));
    m_meshes[1] = MakeMesh(plane, 4, 6, {-0.6f, -0.4f, 0.0f},
                           {0.6f, 0.4f, 0.0f}, {0.0f, 0.0f, 0.0f},
                           std::sqrt(0.6f * 0.6f + 0.4f * 0.4f));
    m_meshes[2] = MakeMesh(pyramid, 5, 18, {-0.6f, -0.5f, -0.6f},
                           {0.6f, 0.8f, 0.6f}, {0.0f, 0.15f, 0.0f},
                           std::sqrt(0.6f * 0.6f + 0.6f * 0.6f + 0.65f * 0.65f));
    m_count = kMaxMeshes;
}

const MeshDesc12* GeometryCatalog12::Find(Resource12 handle) const
{
    for (uint32_t i = 0; i < m_count; ++i)
    {
        if (m_meshes[i].handle == handle)
            return &m_meshes[i];
    }
    return nullptr;
}

int32_t GeometryCatalog12::IndexOf(Resource12 handle) const
{
    for (uint32_t i = 0; i < m_count; ++i)
    {
        if (m_meshes[i].handle == handle)
            return static_cast<int32_t>(i);
    }
    return -1;
}

void GeometryCatalog12::SetViews(uint32_t index,
                                 const D3D12_VERTEX_BUFFER_VIEW& vertexView,
                                 const D3D12_INDEX_BUFFER_VIEW& indexView)
{
    if (index >= m_count)
        return;
    m_meshes[index].vertexView = vertexView;
    m_meshes[index].indexView = indexView;
}

bool ResolveGeometryFrame(const renderprep12::OrderedFrame12& frame,
                          uint64_t liveGeneration,
                          const GeometryCatalog12& catalog,
                          ResolvedFrame12& resolved,
                          const char** reason)
{
    auto fail = [reason](const char* text)
    {
        if (reason)
            *reason = text;
        return false;
    };

    if (!liveGeneration || frame.frame.generation != liveGeneration)
        return fail("stale geometry frame generation");
    if (frame.categories[1].count != 0 || frame.categories[2].count != 0)
        return fail("transparent/hud packets are unsupported");
    if (frame.categories[0].begin != 0 ||
        frame.categories[0].count != frame.frame.packets.size())
        return fail("invalid opaque category range");
    if (frame.frame.packets.size() > dx12object12::kMaxObjects)
        return fail("too many objects for object constant buffer");

    // Transactional resolution: build a fully validated candidate first and
    // publish it only after every packet has passed. A failed resolution must
    // leave the previous frame (and its VBV/IBV) untouched.
    ResolvedFrame12 candidate;
    candidate.generation = frame.frame.generation;
    candidate.draws.resize(frame.frame.packets.size());

    for (size_t i = 0; i < frame.frame.packets.size(); ++i)
    {
        const renderprep12::DrawPacket12& packet = frame.frame.packets[i];
        if (!packet.visible)
            return fail("hidden packet in ordered frame");
        if (packet.object.vertexCount != 0 || packet.object.firstVertex != 0)
            return fail("geometry packet must use indexed range only");
        if (packet.object.indexCount == 0)
            return fail("geometry packet has empty index range");

        const MeshDesc12* mesh = catalog.Find(packet.object.visual);
        if (!mesh)
            return fail("unknown or stale geometry handle");
        if (packet.object.firstIndex != mesh->submesh.firstIndex ||
            packet.object.indexCount != mesh->submesh.indexCount ||
            packet.object.baseVertex != mesh->submesh.baseVertex)
            return fail("index range does not match geometry submesh");
        if (!dx12object12::IsValidTransformWorld(packet.object.world))
            return fail("invalid geometry world transform");
        if (packet.object.material != renderprep12::SyntheticResources12[0] ||
            packet.object.pso != renderprep12::SyntheticResources12[0])
            return fail("unsupported geometry material/pso");

        GpuDraw12& draw = candidate.draws[i];
        draw.meshIndex = static_cast<uint32_t>(
            catalog.IndexOf(packet.object.visual));
        draw.objectConstantIndex = static_cast<uint32_t>(i);
        draw.vertexView = mesh->vertexView;
        draw.indexView = mesh->indexView;
        draw.firstIndex = packet.object.firstIndex;
        draw.indexCount = packet.object.indexCount;
        draw.baseVertex = packet.object.baseVertex;
    }

    resolved = std::move(candidate);
    return true;
}

bool ValidateIndexData(const MeshDesc12& mesh,
                       const uint32_t* indices,
                       size_t count,
                       const char** reason)
{
    auto fail = [reason](const char* text)
    {
        if (reason)
            *reason = text;
        return false;
    };

    if (!indices || count == 0 || count != mesh.indexCount)
        return fail("index data size mismatch");
    if (mesh.vertexCount == 0)
        return fail("mesh has no vertices");

    const uint64_t firstIndex = mesh.submesh.firstIndex;
    const uint64_t submeshCount = mesh.submesh.indexCount;
    if (firstIndex > count || submeshCount > count - firstIndex)
        return fail("submesh range exceeds index data");

    for (size_t i = 0; i < count; ++i)
    {
        const int64_t vertexIndex =
            static_cast<int64_t>(indices[i]) +
            static_cast<int64_t>(mesh.submesh.baseVertex);
        if (vertexIndex < 0 ||
            vertexIndex >= static_cast<int64_t>(mesh.vertexCount))
            return fail("index references a vertex outside the mesh");
    }
    return true;
}

bool Equivalent(const ResolvedFrame12& a, const ResolvedFrame12& b)
{
    if (a.generation != b.generation || a.draws.size() != b.draws.size())
        return false;
    for (size_t i = 0; i < a.draws.size(); ++i)
    {
        const GpuDraw12& x = a.draws[i];
        const GpuDraw12& y = b.draws[i];
        if (x.meshIndex != y.meshIndex ||
            x.pipeline != y.pipeline ||
            x.objectConstantIndex != y.objectConstantIndex ||
            x.firstIndex != y.firstIndex ||
            x.indexCount != y.indexCount ||
            x.baseVertex != y.baseVertex ||
            x.vertexView.BufferLocation != y.vertexView.BufferLocation ||
            x.vertexView.SizeInBytes != y.vertexView.SizeInBytes ||
            x.vertexView.StrideInBytes != y.vertexView.StrideInBytes ||
            x.indexView.BufferLocation != y.indexView.BufferLocation ||
            x.indexView.SizeInBytes != y.indexView.SizeInBytes ||
            x.indexView.Format != y.indexView.Format)
            return false;
    }
    return true;
}
