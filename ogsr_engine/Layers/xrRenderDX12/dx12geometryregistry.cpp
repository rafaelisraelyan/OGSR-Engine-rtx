#include "stdafx.h"
#include "dx12geometryregistry.h"
#include "dx12transformshader.h"
#include "dx12log.h"

#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace
{
struct GeometryVertex12
{
    float x, y, z;
    float r, g, b, a;
};

struct MeshData12
{
    std::vector<GeometryVertex12> vertices;
    std::vector<uint32_t> indices;
};

void AddQuad(MeshData12& mesh, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
    mesh.indices.push_back(a);
    mesh.indices.push_back(c);
    mesh.indices.push_back(d);
}

void BuildCube(MeshData12& mesh)
{
    const float h = 0.5f;
    const GeometryVertex12 p[] = {
        {-h, -h, -h, 1, 1, 1, 1}, { h, -h, -h, 1, 1, 1, 1},
        { h,  h, -h, 1, 1, 1, 1}, {-h,  h, -h, 1, 1, 1, 1},
        {-h, -h,  h, 1, 1, 1, 1}, { h, -h,  h, 1, 1, 1, 1},
        { h,  h,  h, 1, 1, 1, 1}, {-h,  h,  h, 1, 1, 1, 1},
    };
    mesh.vertices.assign(std::begin(p), std::end(p));
    // Front, back, left, right, top, bottom. Cull mode remains NONE.
    AddQuad(mesh, 4, 5, 6, 7);
    AddQuad(mesh, 1, 0, 3, 2);
    AddQuad(mesh, 0, 4, 7, 3);
    AddQuad(mesh, 5, 1, 2, 6);
    AddQuad(mesh, 3, 7, 6, 2);
    AddQuad(mesh, 0, 1, 5, 4);
}

void BuildPlane(MeshData12& mesh)
{
    const GeometryVertex12 p[] = {
        {-0.6f, -0.4f, 0, 1, 1, 1, 1},
        { 0.6f, -0.4f, 0, 1, 1, 1, 1},
        { 0.6f,  0.4f, 0, 1, 1, 1, 1},
        {-0.6f,  0.4f, 0, 1, 1, 1, 1},
    };
    mesh.vertices.assign(std::begin(p), std::end(p));
    AddQuad(mesh, 0, 1, 2, 3);
}

void BuildPyramid(MeshData12& mesh)
{
    const GeometryVertex12 p[] = {
        {-0.6f, -0.5f, -0.6f, 1, 1, 1, 1},
        { 0.6f, -0.5f, -0.6f, 1, 1, 1, 1},
        { 0.6f, -0.5f,  0.6f, 1, 1, 1, 1},
        {-0.6f, -0.5f,  0.6f, 1, 1, 1, 1},
        { 0.0f,  0.8f,  0.0f, 1, 1, 1, 1},
    };
    mesh.vertices.assign(std::begin(p), std::end(p));
    // Base (two triangles) and four sides.
    AddQuad(mesh, 0, 3, 2, 1);
    mesh.indices.insert(mesh.indices.end(),
                        {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4});
}

void BuildMeshData(uint32_t index, MeshData12& mesh)
{
    if (index == 0) BuildCube(mesh);
    else if (index == 1) BuildPlane(mesh);
    else BuildPyramid(mesh);
}

bool CompileShader(const char* source, const char* entryPoint, const char* target,
                   ID3D10Blob** outBlob)
{
    ComPtr<ID3D10Blob> error;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr,
                                  nullptr, entryPoint, target, 0, 0,
                                  outBlob, &error);
    if (FAILED(hr))
    {
        if (error)
            dx12log::Write("[DX12][geometry] shader compile failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        else
            dx12log::Write("[DX12][geometry] shader compile failed, hr=0x%08X",
                           static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}
} // namespace

GpuMeshRegistry12::~GpuMeshRegistry12()
{
    Release();
}

bool GpuMeshRegistry12::CreatePipeline(ID3D12Device* device)
{
    if (!device)
        return false;

    D3D12_ROOT_PARAMETER params[2] = {};
    for (uint32_t i = 0; i < 2; ++i)
    {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].Descriptor.RegisterSpace = 0;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3D10Blob> signature;
    ComPtr<ID3D10Blob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &signature, &error);
    if (FAILED(hr))
    {
        if (error)
            dx12log::Write("[DX12][geometry] root signature failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
                                     signature->GetBufferSize(),
                                     IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr))
        return false;

    ComPtr<ID3D10Blob> vertexShader;
    ComPtr<ID3D10Blob> pixelShader;
    if (!CompileShader(dx12transformshader12::kVertexSource, "main", "vs_5_0",
                       &vertexShader) ||
        !CompileShader(dx12transformshader12::kPixelSource, "main", "ps_5_0",
                       &pixelShader))
        return false;

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_rootSignature.Get();
    desc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    desc.VS.BytecodeLength = vertexShader->GetBufferSize();
    desc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
    desc.PS.BytecodeLength = pixelShader->GetBufferSize();
    desc.InputLayout.pInputElementDescs = inputLayout;
    desc.InputLayout.NumElements = 2;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.NumRenderTargets = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthClipEnable = TRUE;

    hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][geometry] CreateGraphicsPipelineState failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

bool GpuMeshRegistry12::CreateMeshResources(
    ID3D12Device* device,
    ID3D12CommandAllocator* allocator,
    ID3D12GraphicsCommandList* list)
{
    if (!device || !allocator || !list)
        return false;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    std::array<MeshData12, kMaxMeshes> data{};
    for (uint32_t i = 0; i < kMaxMeshes; ++i)
    {
        BuildMeshData(i, data[i]);
        const MeshDesc12& catalog = m_catalog.At(i);
        if (data[i].vertices.size() != catalog.vertexCount ||
            data[i].indices.size() != catalog.indexCount)
        {
            dx12log::Write("[DX12][geometry] catalogue/data mismatch for mesh %u", i);
            return false;
        }
        const char* indexReason = nullptr;
        if (!ValidateIndexData(catalog, data[i].indices.data(),
                               data[i].indices.size(), &indexReason))
        {
            dx12log::Write("[DX12][geometry] invalid index data for mesh %u: %s",
                           i, indexReason ? indexReason : "validation failed");
            return false;
        }

        const UINT64 vertexBytes =
            static_cast<UINT64>(data[i].vertices.size() * sizeof(GeometryVertex12));
        const UINT64 indexBytes =
            static_cast<UINT64>(data[i].indices.size() * sizeof(uint32_t));
        const D3D12_RESOURCE_DESC vertexDesc = BufferDesc(vertexBytes);
        const D3D12_RESOURCE_DESC indexDesc = BufferDesc(indexBytes);

        HRESULT hr = device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&m_meshes[i].vertexBuffer));
        if (FAILED(hr))
            return false;
        hr = device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &indexDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&m_meshes[i].indexBuffer));
        if (FAILED(hr))
            return false;

        hr = device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_meshes[i].uploadVertex));
        if (FAILED(hr))
            return false;
        hr = device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_meshes[i].uploadIndex));
        if (FAILED(hr))
            return false;

        void* mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        hr = m_meshes[i].uploadVertex->Map(0, &readRange, &mapped);
        if (FAILED(hr))
            return false;
        std::memcpy(mapped, data[i].vertices.data(), static_cast<size_t>(vertexBytes));
        m_meshes[i].uploadVertex->Unmap(0, nullptr);
        hr = m_meshes[i].uploadIndex->Map(0, &readRange, &mapped);
        if (FAILED(hr))
            return false;
        std::memcpy(mapped, data[i].indices.data(), static_cast<size_t>(indexBytes));
        m_meshes[i].uploadIndex->Unmap(0, nullptr);

        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = m_meshes[i].vertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes = static_cast<UINT>(vertexBytes);
        vbv.StrideInBytes = sizeof(GeometryVertex12);
        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = m_meshes[i].indexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes = static_cast<UINT>(indexBytes);
        ibv.Format = DXGI_FORMAT_R32_UINT;
        m_catalog.SetViews(i, vbv, ibv);
    }

    HRESULT hr = list->Reset(allocator, nullptr);
    if (FAILED(hr))
        return false;
    for (uint32_t i = 0; i < kMaxMeshes; ++i)
    {
        const UINT64 vertexBytes =
            static_cast<UINT64>(data[i].vertices.size() * sizeof(GeometryVertex12));
        const UINT64 indexBytes =
            static_cast<UINT64>(data[i].indices.size() * sizeof(uint32_t));
        list->CopyBufferRegion(m_meshes[i].vertexBuffer.Get(), 0,
                               m_meshes[i].uploadVertex.Get(), 0, vertexBytes);
        list->CopyBufferRegion(m_meshes[i].indexBuffer.Get(), 0,
                               m_meshes[i].uploadIndex.Get(), 0, indexBytes);

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_meshes[i].vertexBuffer.Get();
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_meshes[i].indexBuffer.Get();
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        list->ResourceBarrier(2, barriers);
    }
    hr = list->Close();
    if (FAILED(hr))
        return false;

    // Create() submits this list after all three mesh uploads are recorded.
    return true;
}

bool GpuMeshRegistry12::Create(ID3D12Device* device,
                               ID3D12CommandQueue* copyQueue,
                               ID3D12CommandAllocator* copyAllocator,
                               ID3D12GraphicsCommandList* copyCommandList)
{
    if (!device || !copyQueue || !copyAllocator || !copyCommandList)
        return false;
    if (m_created || m_copySubmitted)
        return false;
    if (!CreatePipeline(device))
        return false;
    if (!CreateMeshResources(device, copyAllocator, copyCommandList))
        return false;

    // CreateMeshResources closes the list; submit once here.
    ID3D12CommandList* lists[] = {copyCommandList};
    copyQueue->ExecuteCommandLists(1, lists);
    m_copySubmitted = true;
    dx12log::Write("[DX12][geometry] %u indexed meshes submitted, awaiting GPU drain",
                   kMaxMeshes);
    return true;
}

void GpuMeshRegistry12::OnUploadComplete()
{
    if (!m_copySubmitted)
        return;
    for (MeshGpu& mesh : m_meshes)
    {
        mesh.uploadVertex.Reset();
        mesh.uploadIndex.Reset();
    }
    m_copySubmitted = false;
    m_created = true;
}

void GpuMeshRegistry12::Release()
{
    for (MeshGpu& mesh : m_meshes)
    {
        mesh.uploadVertex.Reset();
        mesh.uploadIndex.Reset();
        mesh.vertexBuffer.Reset();
        mesh.indexBuffer.Reset();
    }
    for (uint32_t i = 0; i < kMaxMeshes; ++i)
        m_catalog.SetViews(i, {}, {});
    m_pipelineState.Reset();
    m_rootSignature.Reset();
    m_copySubmitted = false;
    m_created = false;
}

void GpuMeshRegistry12::Quarantine(GpuQuarantine& quarantine)
{
    quarantine.registryPipelineState = m_pipelineState.Detach();
    quarantine.registryRootSignature = m_rootSignature.Detach();
    for (uint32_t i = 0; i < kMaxMeshes; ++i)
    {
        quarantine.registryMeshes[i].vertexBuffer = m_meshes[i].vertexBuffer.Detach();
        quarantine.registryMeshes[i].indexBuffer = m_meshes[i].indexBuffer.Detach();
        quarantine.registryMeshes[i].uploadVertex = m_meshes[i].uploadVertex.Detach();
        quarantine.registryMeshes[i].uploadIndex = m_meshes[i].uploadIndex.Detach();
        m_catalog.SetViews(i, {}, {});
    }
    m_copySubmitted = false;
    m_created = false;
}

bool GpuMeshRegistry12::BuildScene(
    std::vector<renderprep12::Object12>& objects) const
{
    constexpr uint32_t kColumns = 7;
    constexpr uint32_t kRows = 7;
    objects.clear();
    objects.reserve(kColumns * kRows);
    for (uint32_t i = 0; i < kColumns * kRows; ++i)
    {
        const uint32_t geometry = i % m_catalog.Count();
        const MeshDesc12& mesh = m_catalog.At(geometry);
        renderprep12::Object12 object{};
        object.id = static_cast<uint64_t>(i) + 1;
        object.firstVertex = 0;
        object.vertexCount = 0;
        object.firstIndex = mesh.submesh.firstIndex;
        object.indexCount = mesh.submesh.indexCount;
        object.baseVertex = mesh.submesh.baseVertex;
        object.visual = mesh.handle;
        object.material = renderprep12::SyntheticResources12[0];
        object.pso = renderprep12::SyntheticResources12[0];
        object.minimum = mesh.minimum;
        object.maximum = mesh.maximum;
        object.sphere = mesh.sphere;
        object.flags = 0;
        object.opacity = 1.0f;

        const float x = (static_cast<float>(i % kColumns) - 3.0f) * 0.9f;
        const float y = (static_cast<float>(i / kColumns) - 3.0f) * 0.9f;
        const float z = (i % 2) * 0.3f;
        const float scale = 0.36f + 0.01f * static_cast<float>(i % 3);
        const float rotY = (static_cast<float>(i % 5) - 2.0f) * 0.12f;
        const float rotX = (static_cast<float>(i % 7) - 3.0f) * 0.04f;
        const DirectX::XMMATRIX world =
            DirectX::XMMatrixScaling(scale, scale, scale) *
            DirectX::XMMatrixRotationX(rotX) *
            DirectX::XMMatrixRotationY(rotY) *
            DirectX::XMMatrixTranslation(x, y, z);
        DirectX::XMStoreFloat4x4(&object.world, world);
        objects.push_back(object);
    }
    return objects.size() == kColumns * kRows;
}

bool GpuMeshRegistry12::RecordDraw(ID3D12GraphicsCommandList* list,
                                   const GpuDraw12& draw,
                                   D3D12_GPU_VIRTUAL_ADDRESS frameCBVA,
                                   D3D12_GPU_VIRTUAL_ADDRESS objectCBVA) const
{
    if (!list || !m_created || draw.meshIndex >= kMaxMeshes ||
        draw.indexCount == 0)
        return false;

    list->SetGraphicsRootSignature(m_rootSignature.Get());
    list->SetPipelineState(m_pipelineState.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &draw.vertexView);
    list->IASetIndexBuffer(&draw.indexView);
    list->SetGraphicsRootConstantBufferView(0, frameCBVA);
    list->SetGraphicsRootConstantBufferView(1, objectCBVA);
    list->DrawIndexedInstanced(draw.indexCount, 1, draw.firstIndex,
                               draw.baseVertex, 0);
    return true;
}
