#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
void Check(bool b, const char *s)
{
    if (!b)
        throw std::runtime_error(s);
}
void Hr(HRESULT h, const char *s)
{
    if (FAILED(h))
    {
        std::fprintf(stderr, "%s 0x%08x\n", s, (unsigned)h);
        throw std::runtime_error(s);
    }
}
std::string Extract(const std::string &s, const std::string &begin, const std::string &end)
{
    auto a = s.find(begin);
    Check(a != s.npos, "extract begin");
    a += begin.size();
    auto b = s.find(end, a);
    Check(b != s.npos, "extract end");
    return s.substr(a, b - a);
}
void Replace(std::string &s, const std::string &a, const std::string &b)
{
    auto p = s.find(a);
    Check(p != s.npos && s.find(a, p + a.size()) == s.npos, "unique production anchor");
    s.replace(p, a.size(), b);
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device *d, UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_HEAP_PROPERTIES p{};
    p.Type = heap;
    p.CreationNodeMask = p.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC x{};
    x.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    x.Width = bytes;
    x.Height = 1;
    x.DepthOrArraySize = x.MipLevels = 1;
    x.SampleDesc.Count = 1;
    x.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    x.Flags = flags;
    ComPtr<ID3D12Resource> r;
    Hr(d->CreateCommittedResource(&p, D3D12_HEAP_FLAG_NONE, &x, state, nullptr, IID_PPV_ARGS(&r)), "buffer");
    return r;
}
// Растровая проверка вызывает production detail VS и фрагменты production PS.
// Два треугольника имеют противоположный порядок вершин; PS сохраняет реальный SV_IsFrontFace.
void Raster(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *fence, HANDLE event, ID3D12RootSignature *root,
            ID3D12Resource *cb, ID3D12Resource *instances, const std::string &frame, const std::string &detail,
            const std::string &face, const std::string &safe, const std::string &hemi, const std::string &sunCode)
{
    const char *wrapper = R"(
VSOutput RasterVS(uint id:SV_VertexID) {
 const float2 v[6]={float2(-.9,-.8),float2(-.1,-.8),float2(-.5,.8),
                    float2(.1,-.8),float2(.5,.8),float2(.9,-.8)};
 return ProductionDetail(float3(v[id],0),float3(0,1,0),float2(0,0),
                         uint4(0,0,0,0),float4(1,0,0,0),0);
}
float4 RasterPS(VSOutput input, bool frontFace:SV_IsFrontFace):SV_Target {
 FACE_CODE
 float3 h=IndirectHemi(n,input.hemi); SUN_CODE
 return float4(frontFace?1:0,n.x,h.x,sunCosine);
})";
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> heap;
    Hr(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "raster RTV heap");
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    hp.CreationNodeMask = hp.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = 16;
    tex.Height = 8;
    tex.DepthOrArraySize = tex.MipLevels = 1;
    tex.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    tex.SampleDesc.Count = 1;
    tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE optimized{};
    optimized.Format = tex.Format;
    for (float &v : optimized.Color)
        v = -1.f;
    ComPtr<ID3D12Resource> target;
    Hr(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &tex, D3D12_RESOURCE_STATE_RENDER_TARGET, &optimized,
                                  IID_PPV_ARGS(&target)),
       "raster target");
    d->CreateRenderTargetView(target.Get(), nullptr, heap->GetCPUDescriptorHandleForHeapStart());
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 bytes = 0;
    d->GetCopyableFootprints(&tex, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
    auto read = Buffer(d, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    for (int mutation = 0; mutation < 2; ++mutation)
    {
        std::string src = frame + detail + "\nstatic const uint TextureMode=" +
                          (mutation ? std::string("544u;") : std::string("512u;")) + "\n" + safe + hemi;
        std::string w = wrapper;
        Replace(w, "FACE_CODE", face);
        Replace(w, "SUN_CODE", sunCode);
        src += w;
        auto compile = [&](const char *entry, const char *profile) {
            ComPtr<ID3DBlob> code, errors;
            HRESULT h = D3DCompile(src.data(), src.size(), "vegetation-raster.hlsl", nullptr, nullptr, entry,
                                   profile, 0, 0, &code, &errors);
            if (FAILED(h) && errors)
                std::fprintf(stderr, "%s\n", (const char *)errors->GetBufferPointer());
            Hr(h, "raster shader compile");
            return code;
        };
        auto vs = compile("RasterVS", "vs_5_0"), ps = compile("RasterPS", "ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = root;
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable = FALSE;
        pd.DepthStencilState.StencilEnable = FALSE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = tex.Format;
        pd.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pipeline;
        Hr(d->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline)), "raster pipeline");
        ComPtr<ID3D12CommandAllocator> allocator;
        Hr(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
           "raster allocator");
        ComPtr<ID3D12GraphicsCommandList> list;
        Hr(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(),
                                IID_PPV_ARGS(&list)),
           "raster list");
        list->SetGraphicsRootSignature(root);
        list->SetGraphicsRootConstantBufferView(0, cb->GetGPUVirtualAddress());
        unsigned matrix[16]{};
        list->SetGraphicsRoot32BitConstants(1, 16, matrix, 0);
        unsigned geometry[4] = {1, 0, 0, 0};
        list->SetGraphicsRoot32BitConstants(2, 4, geometry, 0);
        list->SetGraphicsRootShaderResourceView(3, instances->GetGPUVirtualAddress());
        D3D12_VIEWPORT viewport{0, 0, 16, 8, 0, 1};
        D3D12_RECT rect{0, 0, 16, 8};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &rect);
        auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
        const float clear[4] = {-1, -1, -1, -1};
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->ClearRenderTargetView(rtv, clear, 0, nullptr);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(6, 1, 0, 0);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {target.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dst{};
        srcLoc.pResource = target.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = read.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        list->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
        Hr(list->Close(), "raster close");
        ID3D12CommandList *lists[] = {list.Get()};
        q->ExecuteCommandLists(1, lists);
        Hr(q->Signal(fence, 5 + mutation), "raster signal");
        Hr(fence->SetEventOnCompletion(5 + mutation, event), "raster fence event");
        Check(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "raster wait");
        Hr(d->GetDeviceRemovedReason(), "raster device");
        const unsigned char *data = nullptr;
        Hr(read->Map(0, nullptr, (void **)&data), "raster map");
        float values[2][4]{};
        for (int i = 0; i < 2; i++)
            std::memcpy(values[i], data + fp.Offset + 4 * fp.Footprint.RowPitch + (i ? 12 : 4) * 16, 16);
        read->Unmap(0, nullptr);
        const bool opposite = std::fabs(values[0][0] - values[1][0]) > .9f;
        const bool parity = std::fabs(values[0][1] - .6f) < .002f && std::fabs(values[1][1] - .6f) < .002f &&
                            std::fabs(values[0][2] - .072997f) < .002f &&
                            std::fabs(values[1][2] - .072997f) < .002f && std::fabs(values[0][3] - 1.f) < .002f &&
                            std::fabs(values[1][3] - 1.f) < .002f;
        std::printf("raster %s: left=(%.3f,%.3f,%.5f,%.3f) right=(%.3f,%.3f,%.5f,%.3f)\n",
                    mutation ? "normal-flip mutation" : "control", values[0][0], values[0][1], values[0][2],
                    values[0][3], values[1][0], values[1][1], values[1][2], values[1][3]);
        Check(opposite && parity == !mutation, "raster front/back parity or mutation");
    }
    std::puts("PASS: raster actual SV_IsFrontFace, slope hemi/sun parity and normal-flip mutation");
}
// Отдельный настоящий texture/SRV проход проверяет production alpha-функции на двух mip.
void AlphaMip(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *fence, HANDLE event, const std::string &source)
{
    auto sample = Extract(source, "float4 SampleBaseColor(float2 uv)\n{", "\n}\n");
    sample = "float4 SampleBaseColor(float2 uv)\n{" + sample + "\n}\n";
    auto clip = Extract(source, "void ApplyAlphaTest(float alpha)\n{", "\n}\n");
    clip = "void ApplyAlphaTest(float alpha)\n{" + clip + "\n}\n";
    auto shadow = Extract(source, "void shadowMain(ShadowInterpolants input)\n{", "\n}\n");
    shadow = "void shadowMain(ShadowInterpolants input)\n{" + shadow + "\n}\n";
    std::string hlsl = R"(
Texture2D BaseColor:register(t0);
SamplerState BaseSampler:register(s0); SamplerState WallmarkSampler:register(s1);
cbuffer MaterialCB:register(b1){uint TextureMode;uint MaterialId;uint ViewMode;float AlphaThreshold;};
)" + sample + clip + R"(
struct V { float4 position:SV_Position; float2 uv:TEXCOORD0; };
V AlphaVS(uint id:SV_VertexID) { V o; float2 p=float2((id<<1)&2,id&2); o.uv=p; o.position=float4(p*float2(2,-2)+float2(-1,1),.25,1); return o; }
float4 AlphaPS(V input):SV_Target { float4 color=SampleBaseColor(input.uv); ApplyAlphaTest(color.a); return color; }
)";
    auto compile = [&](const char *entry, const char *profile) {
        ComPtr<ID3DBlob> code, errors;
        HRESULT h = D3DCompile(hlsl.data(), hlsl.size(), "vegetation-alpha-production.hlsl", nullptr, nullptr,
                               entry, profile, 0, 0, &code, &errors);
        if (FAILED(h) && errors)
            std::fprintf(stderr, "%s\n", (const char *)errors->GetBufferPointer());
        Hr(h, "alpha shader compile");
        return code;
    };
    hlsl += "\nstruct ShadowInterpolants { float4 position:SV_Position; float2 uv:TEXCOORD0; };\n" + shadow;
    auto vs = compile("AlphaVS", "vs_5_0"), ps = compile("AlphaPS", "ps_5_0"),
         shadowPs = compile("shadowMain", "ps_5_0");
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER p[2]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants = {1, 0, 4};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[1].DescriptorTable = {1, &range};
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (unsigned i = 0; i < 2; i++)
    {
        samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].ShaderRegister = i;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = p;
    desc.NumStaticSamplers = 2;
    desc.pStaticSamplers = samplers;
    ComPtr<ID3DBlob> rsblob, errors;
    Hr(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &rsblob, &errors), "alpha root serialize");
    ComPtr<ID3D12RootSignature> root;
    Hr(d->CreateRootSignature(0, rsblob->GetBufferPointer(), rsblob->GetBufferSize(), IID_PPV_ARGS(&root)),
       "alpha root");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    Hr(d->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline)), "alpha pipeline");
    pd.PS = {shadowPs->GetBufferPointer(), shadowPs->GetBufferSize()};
    pd.NumRenderTargets = 0;
    pd.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ComPtr<ID3D12PipelineState> shadowPipeline;
    Hr(d->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&shadowPipeline)), "alpha shadow pipeline");
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    hp.CreationNodeMask = hp.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = 8;
    texture.Height = 2;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 2;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> image;
    Hr(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                  IID_PPV_ARGS(&image)),
       "alpha texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2]{};
    UINT64 bytes = 0;
    d->GetCopyableFootprints(&texture, 0, 2, 0, footprints, nullptr, nullptr, &bytes);
    auto upload = Buffer(d, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    unsigned char *mapped = nullptr;
    Hr(upload->Map(0, nullptr, (void **)&mapped), "alpha upload map");
    std::memset(mapped, 0, static_cast<size_t>(bytes));
    const unsigned char alpha[4] = {0, 199, 201, 255};
    for (unsigned mip = 0; mip < 2; mip++)
        for (unsigned y = 0; y < (mip ? 1u : 2u); y++)
            for (unsigned x = 0; x < (mip ? 4u : 8u); x++)
            {
                auto *pixel = mapped + footprints[mip].Offset + y * footprints[mip].Footprint.RowPitch + x * 4;
                pixel[0] = pixel[1] = pixel[2] = 255;
                pixel[3] = alpha[mip ? 3 - x : x / 2];
            }
    upload->Unmap(0, nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC sh{};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 2;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> srv;
    Hr(d->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&srv)), "alpha srv heap");
    const UINT increment = d->GetDescriptorHandleIncrementSize(sh.Type);
    auto cpu = srv->GetCPUDescriptorHandleForHeapStart();
    for (UINT mip = 0; mip < 2; mip++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = texture.Format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MostDetailedMip = mip;
        view.Texture2D.MipLevels = 1;
        d->CreateShaderResourceView(image.Get(), &view, cpu);
        cpu.ptr += increment;
    }
    D3D12_RESOURCE_DESC rt = texture;
    rt.Width = 4;
    rt.Height = 1;
    rt.MipLevels = 1;
    rt.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = rt.Format;
    ComPtr<ID3D12Resource> target;
    Hr(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rt, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                                  IID_PPV_ARGS(&target)),
       "alpha target");
    D3D12_DESCRIPTOR_HEAP_DESC rh{};
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rh.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Hr(d->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtvHeap)), "alpha rtv heap");
    auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    d->CreateRenderTargetView(target.Get(), nullptr, rtv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT rfp{};
    UINT64 rbytes = 0;
    d->GetCopyableFootprints(&rt, 0, 1, 0, &rfp, nullptr, nullptr, &rbytes);
    auto read = Buffer(d, rbytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_RESOURCE_DESC depthDesc = rt;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = depthDesc.Format;
    depthClear.DepthStencil.Depth = 1.f;
    ComPtr<ID3D12Resource> depth;
    Hr(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                  &depthClear, IID_PPV_ARGS(&depth)),
       "alpha shadow depth");
    D3D12_DESCRIPTOR_HEAP_DESC dh{};
    dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dh.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Hr(d->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&dsvHeap)), "alpha DSV heap");
    auto dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    d->CreateDepthStencilView(depth.Get(), nullptr, dsv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT dfp{};
    UINT64 dbytes = 0;
    d->GetCopyableFootprints(&depthDesc, 0, 1, 0, &dfp, nullptr, nullptr, &dbytes);
    auto depthRead = Buffer(d, dbytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    for (unsigned mip = 0; mip < 2; mip++)
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        Hr(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "alpha allocator");
        ComPtr<ID3D12GraphicsCommandList> list;
        Hr(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(),
                                IID_PPV_ARGS(&list)),
           "alpha list");
        if (mip == 0)
        {
            for (UINT i = 0; i < 2; i++)
            {
                D3D12_TEXTURE_COPY_LOCATION a{}, b{};
                a.pResource = upload.Get();
                a.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                a.PlacedFootprint = footprints[i];
                b.pResource = image.Get();
                b.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                b.SubresourceIndex = i;
                list->CopyTextureRegion(&b, 0, 0, 0, &a, nullptr);
            }
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {image.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
            list->ResourceBarrier(1, &b);
        }
        list->SetGraphicsRootSignature(root.Get());
        ID3D12DescriptorHeap *heaps[] = {srv.Get()};
        list->SetDescriptorHeaps(1, heaps);
        struct
        {
            unsigned mode, id, view;
            float threshold;
        } material{2, 0, 0, 200.f / 255.f};
        list->SetGraphicsRoot32BitConstants(0, 4, &material, 0);
        auto gpu = srv->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(mip) * increment;
        list->SetGraphicsRootDescriptorTable(1, gpu);
        D3D12_VIEWPORT viewport{0, 0, 4, 1, 0, 1};
        D3D12_RECT rect{0, 0, 4, 1};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &rect);
        const float clear[4] = {0, 0, 0, 0};
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->ClearRenderTargetView(rtv, clear, 0, nullptr);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(3, 1, 0, 0);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {target.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = target.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = read.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = rfp;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
        list->SetPipelineState(shadowPipeline.Get());
        list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
        list->DrawInstanced(3, 1, 0, 0);
        barrier.Transition = {depth.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION depthSrc{}, depthDst{};
        depthSrc.pResource = depth.Get();
        depthSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        depthDst.pResource = depthRead.Get();
        depthDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        depthDst.PlacedFootprint = dfp;
        list->CopyTextureRegion(&depthDst, 0, 0, 0, &depthSrc, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
        Hr(list->Close(), "alpha close");
        ID3D12CommandList *lists[] = {list.Get()};
        q->ExecuteCommandLists(1, lists);
        Hr(q->Signal(fence, 7 + mip), "alpha signal");
        Hr(fence->SetEventOnCompletion(7 + mip, event), "alpha fence");
        Check(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "alpha wait");
        Hr(d->GetDeviceRemovedReason(), "alpha device");
        const unsigned char *data = nullptr;
        Hr(read->Map(0, nullptr, (void **)&data), "alpha readback");
        data += rfp.Offset;
        std::printf("alpha mip=%u output R=%u,%u,%u,%u A=%u,%u,%u,%u\n", mip, data[0], data[4], data[8], data[12],
                    data[3], data[7], data[11], data[15]);
        Check(mip == 0 ? (data[0] == 0 && data[4] == 0 && data[8] == 255 && data[12] == 255 && data[11] == 201 &&
                          data[15] == 255)
                       : (data[0] == 255 && data[4] == 255 && data[8] == 0 && data[12] == 0 && data[3] == 255 &&
                          data[7] == 201),
              "alpha mip threshold readback");
        bool colorMask[4]{};
        for (unsigned x = 0; x < 4; x++)
            colorMask[x] = data[x * 4] != 0;
        read->Unmap(0, nullptr);
        const unsigned char *depthBytes = nullptr;
        Hr(depthRead->Map(0, nullptr, (void **)&depthBytes), "alpha depth readback");
        depthBytes += dfp.Offset;
        float z[4]{};
        std::memcpy(z, depthBytes, sizeof(z));
        for (unsigned x = 0; x < 4; x++)
            Check(std::fabs(z[x] - (colorMask[x] ? .25f : 1.f)) < 1.e-6f, "color/shadow depth mask parity");
        std::printf("shadow mip=%u depth=%.2f,%.2f,%.2f,%.2f mask parity PASS\n", mip, z[0], z[1], z[2], z[3]);
        depthRead->Unmap(0, nullptr);
    }
    std::puts(
        "PASS: production SampleBaseColor/ApplyAlphaTest, 200/255 threshold, separate SRVs and two mip readbacks");
}
int main(int argc, char **argv)
{
    try
    {
        Check(argc == 2, "expected StaticScene.cpp path");
        std::ifstream file(argv[1], std::ios::binary);
        Check(file.good(), "open production source");
        std::string source{std::istreambuf_iterator<char>(file), {}};
        auto frame = Extract(source, "constexpr const char* kFrameShaderConstants = R\"(\n", "\n)\";");
        auto tree = Extract(source, "constexpr const char* kSceneVertexShader = R\"(\n", "\n)\";");
        auto detail = Extract(source, "constexpr const char* kAuxVertexShader = R\"(\n", "\n)\";");
        auto hemi = Extract(source, "float3 IndirectHemi(float3 normal, float bakedHemi)\n{", "\n}\n");
        hemi = "float3 IndirectHemi(float3 normal, float bakedHemi)\n{" + hemi + "\n}\n";
        auto face = Extract(source, "    const bool flipNormal = ", "\n    float4 baseColor");
        face = "const bool flipNormal = " + face;
        auto sunCode = Extract(source, "    const float sunCosine = ", ";\n");
        sunCode = "const float sunCosine = " + sunCode + ";";
        auto safe = Extract(source, "float3 SafeDirection(float3 value)\n{", "\n}\n");
        safe = "float3 SafeDirection(float3 value)\n{" + safe + "\n}\n";
        Replace(tree, "VSOutput main(VSInput input)", "VSOutput ProductionTree(VSInput input)");
        Replace(detail, "VSOutput main(float3 position", "VSOutput ProductionDetail(float3 position");
        ComPtr<ID3D12Debug> debug;
        Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "debug");
        debug->EnableDebugLayer();
        ComPtr<ID3D12Debug1> debug1;
        Hr(debug.As(&debug1), "GBV interface");
        debug1->SetEnableGPUBasedValidation(TRUE);
        ComPtr<IDXGIFactory4> factory;
        Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
        ComPtr<IDXGIAdapter> adapter;
        Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "WARP adapter");
        ComPtr<ID3D12Device> d;
        Hr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d)), "device");
        ComPtr<ID3D12InfoQueue> info;
        Hr(d.As(&info), "info queue");
        info->ClearStorageFilter();
        info->ClearRetrievalFilter();
        const auto denied = info->GetNumMessagesDeniedByStorageFilter();
        D3D12_ROOT_PARAMETER p[5]{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p[0].Descriptor.ShaderRegister = 0;
        p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[1].Constants = {3, 0, 16};
        p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[2].Constants = {4, 0, 4};
        p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[3].Descriptor.ShaderRegister = 1;
        p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[4].Descriptor.ShaderRegister = 0;
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.NumParameters = 5;
        rs.pParameters = p;
        ComPtr<ID3DBlob> blob, err;
        Hr(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "serialize root");
        ComPtr<ID3D12RootSignature> root;
        Hr(d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)),
           "root");
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> q;
        Hr(d->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)), "queue");
        ComPtr<ID3D12Fence> fence;
        Hr(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        Check(event != nullptr, "event");
        auto cb = Buffer(d.Get(), 768, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        float *c = nullptr;
        Hr(cb->Map(0, nullptr, (void **)&c), "map constants");
        std::memset(c, 0, 768);
        for (int i = 0; i < 4; i++)
            c[i * 5] = 1;
        c[84] = c[85] = c[86] = 0.1f;
        c[88] = -.6f;
        c[89] = -.8f;
        reinterpret_cast<unsigned *>(c)[97] = 1;
        c[100] = c[101] = c[102] = 0.6f;
        c[103] = 1;
        cb->Unmap(0, nullptr);
        auto input = Buffer(d.Get(), 128, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        float *m = nullptr;
        Hr(input->Map(0, nullptr, (void **)&m), "map instance");
        std::memset(m, 0, 128);
        for (int j = 0; j < 2; j++)
        {
            float *t = m + j * 16;
            t[0] = t[5] = t[10] = 1;
            t[3] = 0.6f;
            t[7] = 0.8f;
            t[11] = 0;
            t[15] = j == 0 ? 0.4f : 0.8f;
        }
        input->Unmap(0, nullptr);
        auto output = Buffer(d.Get(), 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto read = Buffer(d.Get(), 256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        const char *tail = R"(
RWStructuredBuffer<float4> Result:register(u0);
[numthreads(4,1,1)] void Test(uint3 id:SV_DispatchThreadID){
#ifdef TREE
 VSInput i=(VSInput)0; i.position=float3(0,0,0); i.nh=float4(0.5,0.5,1.0,(id.x+1)*0.2); VSOutput v=ProductionTree(i);
#else
 VSOutput v=ProductionDetail(float3(0,0,0),float3(0,1,0),float2(0,0),uint4(0,0,0,0),float4(1,0,0,0),id.x/2);
#endif
 bool frontFace=(id.x%2)==0; PSInput input; input.normal=v.normal; FACE_CODE
 float3 h=IndirectHemi(n,v.hemi);SUN_CODE float sun=sunCosine;
 Result[id.x*2]=float4(n,v.hemi); Result[id.x*2+1]=float4(h,sun);
})";
        for (int mode = 0; mode < 4; mode++)
        {
            bool isTree = mode == 0 || mode == 2;
            std::string hlsl = frame + (isTree ? tree : detail);
            if (mode == 2)
                Replace(hlsl, "input.nh.w * asfloat(AdvancedWaterGeometry) + PuddleHeight", "input.nh.w");
            if (mode == 3)
                Replace(hlsl, "float3(detail.row0.w, detail.row1.w, detail.row2.w)", "float3(0, 1, 0)");
            hlsl += "\nstatic const uint TextureMode=512u; struct PSInput {float3 normal;};\n" + safe + hemi;
            std::string end = tail;
            Replace(end, "FACE_CODE", face);
            Replace(end, "SUN_CODE", sunCode);
            hlsl += end;
            D3D_SHADER_MACRO defs[] = {{isTree ? "TREE" : nullptr, isTree ? "1" : nullptr}, {nullptr, nullptr}};
            ComPtr<ID3DBlob> shader, errors;
            auto ch = D3DCompile(hlsl.data(), hlsl.size(), "production-vegetation.hlsl", defs, nullptr, "Test",
                                 "cs_5_0", 0, 0, &shader, &errors);
            if (FAILED(ch) && errors)
                std::fprintf(stderr, "%s\n", (const char *)errors->GetBufferPointer());
            Hr(ch, "compile production HLSL");
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = root.Get();
            pd.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
            ComPtr<ID3D12PipelineState> pipe;
            Hr(d->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipe)), "pipeline");
            ComPtr<ID3D12CommandAllocator> alloc;
            Hr(d->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&alloc)), "allocator");
            ComPtr<ID3D12GraphicsCommandList> cl;
            Hr(d->CreateCommandList(0, qd.Type, alloc.Get(), pipe.Get(), IID_PPV_ARGS(&cl)), "list");
            if (mode == 0)
            {
                D3D12_RESOURCE_BARRIER initial{};
                initial.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                initial.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                      D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
                cl->ResourceBarrier(1, &initial);
            }
            cl->SetComputeRootSignature(root.Get());
            cl->SetComputeRootConstantBufferView(0, cb->GetGPUVirtualAddress());
            float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            if (!isTree)
                world[0] = 0;
            cl->SetComputeRoot32BitConstants(1, 16, world, 0);
            struct
            {
                unsigned tree;
                float shift;
                unsigned third;
                float fourth;
            } geo{isTree ? 1u : 1u, 0, 0, 0};
            if (isTree)
            {
                float f = .4f * (.5f * 1.5f * 1.3333f);
                std::memcpy(&geo.third, &f, 4);
                geo.fourth = .25f * (.5f * 1.5f * 1.3333f);
            }
            cl->SetComputeRoot32BitConstants(2, 4, &geo, 0);
            cl->SetComputeRootShaderResourceView(3, input->GetGPUVirtualAddress());
            cl->SetComputeRootUnorderedAccessView(4, output->GetGPUVirtualAddress());
            cl->Dispatch(1, 1, 1);
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
            cl->ResourceBarrier(1, &b);
            cl->CopyBufferRegion(read.Get(), 0, output.Get(), 0, 128);
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
            cl->ResourceBarrier(1, &b);
            Hr(cl->Close(), "close");
            ID3D12CommandList *lists[] = {cl.Get()};
            q->ExecuteCommandLists(1, lists);
            Hr(q->Signal(fence.Get(), mode + 1), "signal");
            Hr(fence->SetEventOnCompletion(mode + 1, event), "event fence");
            Check(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "wait");
            Hr(d->GetDeviceRemovedReason(), "device removed");
            float *out = nullptr;
            Hr(read->Map(0, nullptr, (void **)&out), "map result");
            bool correct = true;
            for (int i = 0; i < 4; i++)
            {
                std::printf("  %s case=%d front=%d normal=(%.4f,%.4f,%.4f) baked=%.4f hemi=%.6f sun=%.6f\n",
                            isTree ? "tree" : "detail", i, (i % 2) == 0, out[i * 8], out[i * 8 + 1],
                            out[i * 8 + 2], out[i * 8 + 3], out[i * 8 + 4], out[i * 8 + 7]);
                float expected =
                    isTree ? ((i + 1) * .2f * .4f + .25f) * (.5f * 1.5f * 1.3333f) : (i < 2 ? .4f : .8f);
                float nx = isTree ? 1.f : .6f, ny = isTree ? 0.f : .8f;
                float weight = expected * (.5f + .5f * ny);
                float h = std::pow(.1f + .6f * weight, 2.2f) - std::pow(.1f, 2.2f);
                correct &= std::fabs(out[i * 8] - nx) < .002f && std::fabs(out[i * 8 + 1] - ny) < .002f &&
                           std::fabs(out[i * 8 + 3] - expected) < .002f && std::fabs(out[i * 8 + 4] - h) < .002f &&
                           std::fabs(out[i * 8 + 7] - (isTree ? .6f : 1.f)) < .002f;
            }
            read->Unmap(0, nullptr);
            std::printf("%s %s\n",
                        mode == 0   ? "tree control"
                        : mode == 1 ? "detail control"
                        : mode == 2 ? "tree no-scale mutation"
                                    : "detail vertical-normal mutation",
                        (correct == (mode < 2)) ? "PASS" : "FAIL");
            Check(correct == (mode < 2), "readback or mutation oracle");
        }
        Raster(d.Get(), q.Get(), fence.Get(), event, root.Get(), cb.Get(), input.Get(), frame, detail, face, safe,
               hemi, sunCode);
        AlphaMip(d.Get(), q.Get(), fence.Get(), event, source);
        const auto count = info->GetNumStoredMessages();
        Check(info->GetNumMessagesDeniedByStorageFilter() == denied &&
                  info->GetNumMessagesDiscardedByMessageCountLimit() == 0,
              "debug message coverage");
        for (UINT64 i = 0; i < count; i++)
        {
            SIZE_T n = 0;
            Hr(info->GetMessage(i, nullptr, &n), "message size");
            std::vector<unsigned char> x(n);
            auto *msg = (D3D12_MESSAGE *)x.data();
            Hr(info->GetMessage(i, msg, &n), "message");
            if (msg->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                std::fprintf(stderr, "%s\n", msg->pDescription);
                throw std::runtime_error("debug validation message");
            }
        }
        CloseHandle(event);
        std::puts("PASS: WARP Debug Layer and GBV, compute and raster readback");
        return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
