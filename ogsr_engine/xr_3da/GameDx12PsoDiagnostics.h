#pragma once

#include <d3d12.h>
#include <d3d12shader.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gamepso12
{
inline uint64_t BytecodeHash(const void* data, size_t size) noexcept
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

struct ShaderConfig
{
    unsigned legacyLights, sunRadius, localRadius;
    bool pageTable;
    UINT Flags() const { return pageTable ? D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES : 0; }
};

template<class Log>
void DescribeShader(D3D12_SHADER_BYTECODE code, const char* stage, Log log)
{
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    const HRESULT hr = D3DReflect(code.pShaderBytecode, code.BytecodeLength, IID_PPV_ARGS(&reflection));
    D3D12_SHADER_DESC desc{};
    if (FAILED(hr) || FAILED(reflection->GetDesc(&desc)))
    {
        log("ph_dx12_pso_reflect: stage=%s unavailable HRESULT=0x%08X", stage, static_cast<unsigned>(hr));
        return;
    }
    log("ph_dx12_pso_reflect: stage=%s bytes=%zu hash_fnv1a=%016llx instructions=%u flow_static=%u flow_dynamic=%u",
        stage, code.BytecodeLength, static_cast<unsigned long long>(BytecodeHash(code.pShaderBytecode, code.BytecodeLength)),
        desc.InstructionCount, desc.StaticFlowControlCount, desc.DynamicFlowControlCount);
    for (UINT i = 0; i < desc.BoundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        if (SUCCEEDED(reflection->GetResourceBindingDesc(i, &binding)))
            log("ph_dx12_pso_bind: stage=%s name=%s type=%u register=%u space=%u count=%u dimension=%u",
                stage, binding.Name, binding.Type, binding.BindPoint, binding.Space, binding.BindCount, binding.Dimension);
    }
    for (bool input : {true, false})
    {
        const UINT count = input ? desc.InputParameters : desc.OutputParameters;
        for (UINT i = 0; i < count; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC parameter{};
            if (SUCCEEDED(input ? reflection->GetInputParameterDesc(i, &parameter) :
                reflection->GetOutputParameterDesc(i, &parameter)))
                log("ph_dx12_pso_signature: stage=%s direction=%s semantic=%s%u register=%u mask=%x rw_mask=%x type=%u system=%u",
                    stage, input ? "in" : "out", parameter.SemanticName, parameter.SemanticIndex,
                    parameter.Register, parameter.Mask, parameter.ReadWriteMask,
                    parameter.ComponentType, parameter.SystemValueType);
        }
    }
}

template<class Log>
HRESULT Create(ID3D12Device& device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
    ID3D12PipelineState** output, const char* identity, const char* vertexEntry, const char* pixelEntry,
    const ShaderConfig& config, uint64_t rootHash, std::string& reason, Log log)
{
    using Microsoft::WRL::ComPtr;
    const bool trace = std::getenv("OGSR_DX12_PSO_TRACE") != nullptr;
    if (trace)
        log("ph_dx12_pso_create: PSO=%s", identity);
    const HRESULT hr = device.CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(output));
    if (SUCCEEDED(hr))
        return hr;

    char summary[160]{};
    std::snprintf(summary, sizeof(summary), "CreateGraphicsPipelineState PSO=%s HRESULT=0x%08X",
        identity, static_cast<unsigned>(hr));
    reason = summary;
    log("ph_dx12_pso: %s root_fnv1a=%016llx VS=%s/vs_5_0 PS=%s/%s removed_reason=0x%08X "
        "VS_bytes=%zu PS_bytes=%zu VS_hash=%016llx PS_hash=%016llx",
        summary, static_cast<unsigned long long>(rootHash), vertexEntry, pixelEntry,
        config.pageTable ? "ps_5_1" : "ps_5_0", static_cast<unsigned>(device.GetDeviceRemovedReason()),
        desc.VS.BytecodeLength, desc.PS.BytecodeLength,
        static_cast<unsigned long long>(BytecodeHash(desc.VS.pShaderBytecode, desc.VS.BytecodeLength)),
        static_cast<unsigned long long>(BytecodeHash(desc.PS.pShaderBytecode, desc.PS.BytecodeLength)));
    log("ph_dx12_pso_macros: LEGACY_DIAGNOSTIC_LIGHT_SCALE=%u OGSR_DX12_CONTINUOUS_SUN=%u "
        "OGSR_DX12_CONTINUOUS_LOCAL=%u OGSR_DX12_SHADOW_PAGE_TABLE=%s vs_flags=0 ps_flags=0x%x",
        config.legacyLights, config.sunRadius, config.localRadius, config.pageTable ? "1" : "undefined", config.Flags());

    const LUID luid = device.GetAdapterLuid();
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    LARGE_INTEGER driver{};
    HRESULT driverResult = E_NOINTERFACE;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
    {
        adapter->GetDesc1(&adapterDesc);
        driverResult = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    device.CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
    for (auto model : {D3D_SHADER_MODEL_6_8, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1})
    {
        shaderModel.HighestShaderModel = model;
        if (SUCCEEDED(device.CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
            break;
        shaderModel.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0);
    }
    log("ph_dx12_pso_adapter: luid=%08x:%08x vendor=0x%04x device=0x%04x driver=0x%016llx driver_hresult=0x%08X "
        "binding_tier=%u shader_model=0x%x", static_cast<unsigned>(luid.HighPart), luid.LowPart,
        adapterDesc.VendorId, adapterDesc.DeviceId, static_cast<unsigned long long>(driver.QuadPart),
        static_cast<unsigned>(driverResult), options.ResourceBindingTier, shaderModel.HighestShaderModel);
    ComPtr<ID3D12InfoQueue> queue;
    device.QueryInterface(IID_PPV_ARGS(&queue));
    if (queue)
    {
        const UINT64 last = queue->GetNumStoredMessagesAllowedByRetrievalFilter();
        log("ph_dx12_pso_infoqueue: stored_messages=%llu", static_cast<unsigned long long>(last));
        for (UINT64 i = 0; i < last; ++i)
        {
            SIZE_T size = 0;
            if (FAILED(queue->GetMessage(i, nullptr, &size)) || !size)
                continue;
            std::vector<uint8_t> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(queue->GetMessage(i, message, &size)))
                log("ph_dx12_pso_infoqueue: id=%u severity=%u %s", message->ID, message->Severity, message->pDescription);
        }
    }
    else
        log("ph_dx12_pso_infoqueue: unavailable");
    DescribeShader(desc.VS, "VS", log);
    DescribeShader(desc.PS, "PS", log);
    return hr;
}
}
