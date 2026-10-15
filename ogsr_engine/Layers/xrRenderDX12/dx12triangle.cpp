#include "stdafx.h"
#include "dx12triangle.h"
#include "dx12log.h"

#include <d3dcompiler.h>
#include <cstring>

// Вершины в NDC: треугольник в центре окна, z = 0 попадает внутрь
// [-1, 1] depth range. Для F3.0 достаточно одного атрибута POSITION.
// Winding order намеренно не важен: PSO создаётся с CullMode = NONE.
struct Vertex
{
    float x, y, z;
};

static const Vertex kTriangle[3] = {
    { 0.0f,  0.6f, 0.0f},  // верхняя вершина
    {-0.55f, -0.45f, 0.0f},  // нижняя левая
    { 0.55f, -0.45f, 0.0f},  // нижняя правая
};

// Шейдеры держим строками в коде: отдельный .hlsl потребовал бы либо
// компиляции на этапе сборки, либо пути к файлу в рантайме. Для одного
// треугольника строковый вариант проще и не добавляет зависимостей.
static const char* kVertexShaderSrc = R"(
struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    return output;
}
)";

static const char* kPixelShaderSrc = R"(
struct PSInput
{
    float4 position : SV_Position;
};

float4 main(PSInput input) : SV_Target
{
    return float4(1.0f, 0.55f, 0.12f, 1.0f);
}
)";

namespace
{
bool CompileShader(
    const char* source,
    const char* entryPoint,
    const char* target,
    ID3D10Blob** outBlob)
{
    ComPtr<ID3D10Blob> errorBlob;

    // D3DCompile из d3dcompiler.lib: тот же компилятор, что использует
    // DX11-путь проекта. DXBC (sm_5_0) принимается DX12 raster'ом;
    // DXC и DXIL на этом этапе не нужны и не подключаются.
    HRESULT hr = D3DCompile(
        source,
        strlen(source),
        "dx12triangle_inline",
        nullptr,
        nullptr,
        entryPoint,
        target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        outBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            dx12log::Write("[DX12][triangle] shader compile failed (%s / %s), hr=0x%08X: %s",
                           entryPoint, target, static_cast<unsigned>(hr),
                           static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        else
        {
            dx12log::Write("[DX12][triangle] shader compile failed (%s / %s), hr=0x%08X, no diagnostic",
                           entryPoint, target, static_cast<unsigned>(hr));
        }
        return false;
    }

    return true;
}
}  // namespace

SmokeTriangle12::~SmokeTriangle12()
{
    Release();
}

bool SmokeTriangle12::Create(
    ID3D12Device* device,
    ID3D12CommandQueue* copyQueue,
    ID3D12CommandAllocator* copyAllocator,
    ID3D12GraphicsCommandList* copyCommandList)
{
    if (m_created)
    {
        dx12log::Write("[DX12][triangle] Create called twice, refusing");
        return false;
    }

    if (!device || !copyQueue || !copyAllocator || !copyCommandList)
    {
        dx12log::Write("[DX12][triangle] Create refused: null dependency");
        return false;
    }

    if (!CreateRootSignature(device))
        return false;

    if (!CreatePipelineState(device))
        return false;

    if (!CreateVertexBuffer(device))
        return false;

    // Загрузка вершин через upload heap. Ожидание GPU выполняет вызывающий
    // код (CHW12::Initialize) через WaitForGpu: компонент не владеет fence,
    // чтобы все значения сигналов остались в одном месте. Иначе значения
    // треугольника и RenderFrame столкнулись бы на одном fence.
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = sizeof(kTriangle);
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_uploadBuffer));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][triangle] CreateCommittedResource(upload) failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    hr = m_uploadBuffer->Map(0, &readRange, &mapped);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][triangle] upload buffer Map failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        m_uploadBuffer.Reset();
        return false;
    }
    memcpy(mapped, kTriangle, sizeof(kTriangle));
    m_uploadBuffer->Unmap(0, nullptr);

    // Командный лист для копии берём у вызывающего: CHW12::CreateFrameResources
    // создаёт закрытые листы, а Reset() в них был бы первым обращением из
    // RenderFrame. Свой отдельный лист потребовал бы отдельного аллокатора.
    hr = copyCommandList->Reset(copyAllocator, nullptr);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][triangle] copy command list Reset failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        m_uploadBuffer.Reset();
        return false;
    }

    copyCommandList->CopyBufferRegion(
        m_vertexBuffer.Get(), 0, m_uploadBuffer.Get(), 0, sizeof(kTriangle));

    // StateBefore = COPY_DEST, хотя буфер создан в COMMON: при первом
    // использовании в CopyBufferRegion выше Debug Layer неявно продвинул
    // COMMON -> COPY_DEST и отслеживает состояние как COPY_DEST
    // ("assumed at first use"). Подробнее — в CreateVertexBuffer.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_vertexBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    copyCommandList->ResourceBarrier(1, &barrier);

    hr = copyCommandList->Close();
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][triangle] copy command list Close failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        m_uploadBuffer.Reset();
        return false;
    }

    // ExecuteCommandLists возвращает void (проверено по d3d12.h, Windows SDK
    // 10.0.26100.0): HRESULT здесь получить невозможно, и проверять нечего.
    //
    // Runtime может отклонить command list без возвращаемого результата.
    // Обнаружить такое отклонение можно только через предусмотренную DX12
    // диагностику, и всё она уже проверяется в этом пути:
    //  - HRESULT Close() выше — лист принят runtime'ом;
    //  - HRESULT Signal() в WaitForGpu() вызывающего кода — очередь жива;
    //  - HRESULT SetEventOnCompletion() в WaitForFenceValue();
    //  - fence completion + CheckFenceForRemoval (UINT64_MAX = device removed);
    //  - Debug Layer / InfoQueue через DrainInfoQueue().
    //
    // Утверждение "отклонённая команда обязательно даёт 5-секундный таймаут
    // fence" было бы неверным: из API это не следует. Отклонение может
    // проявиться как ошибка Close/Signal, как validation-сообщение в
    // InfoQueue, как device removed на fence, или не проявиться вовсе.
    // Поэтому здесь не делаем предположений о характере сбоя, а полагаемся
    // на фактический результат ожидания, которое выполняет вызывающий код.
    ID3D12CommandList* lists[] = {copyCommandList};
    copyQueue->ExecuteCommandLists(1, lists);

    // Команды копии переданы в очередь. GPU может их не выполнить, поэтому
    // upload buffer освобождает OnUploadComplete(), который вызывающий код
    // вызовет только после успешного WaitForGpu(): до этого GPU ещё читает
    // из upload buffer.
    m_copySubmitted = true;
    dx12log::Write("[DX12][triangle] vertex upload submitted, awaiting GPU drain");
    return true;
}

// Вызывается из CHW12::Initialize после Create и успешного WaitForGpu.
// GPU завершил копирование, поэтому upload buffer можно освободить.
void SmokeTriangle12::OnUploadComplete()
{
    if (!m_copySubmitted)
        return;

    m_uploadBuffer.Reset();
    m_copySubmitted = false;
    m_created = true;

    dx12log::Write("[DX12][triangle] created: 3 vertices, root signature and PSO ready");
}

bool SmokeTriangle12::CreateVertexBuffer(ID3D12Device* device)
{
    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeof(kTriangle);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // COMMON, а не COPY_DEST. Причина по документации и подтверждена тестом:
    //
    // 1. Для буферов InitialResourceState игнорируется: они всегда эффективно
    //    создаются в COMMON. Явный COPY_DEST даёт
    //    WARNING #1328 CREATERESOURCE_STATE_IGNORED
    //    ("Ignoring InitialState COPY_DEST. Buffers are effectively created
    //    in state COMMON") — предупреждение о состоянии, которое runtime
    //    всё равно не применяет.
    // 2. При GPU-Based Validation ресурс, используемый в COPY-командах,
    //    должен стартовать в COMMON. COPY_DEST как InitialState даёт
    //    EXECUTION ERROR #942 GPU_BASED_VALIDATION_INCOMPATIBLE_RESOURCE_STATE
    //    ("Resources used in COPY command lists must start out in the
    //    COMMON state. This includes Resources created in a COPY_SOURCE or
    //    COPY_DEST state"). В нашем случае копия идёт через графическую
    //    очередь, поэтому ошибка не проявилась, но COMMON корректен всегда.
    //
    // Барьер ниже намеренно объявляет StateBefore = COPY_DEST: при первом
    // использовании буфера в CopyBufferRegion Debug Layer неявно продвигает
    // COMMON -> COPY_DEST и отслеживает состояние как COPY_DEST
    // ("assumed at first use"). Если объявить в барьере COMMON, получим
    // ERROR #527 RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH — именно это и
    // произошло при первой попытке, когда менялись оба значения сразу.
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_vertexBuffer));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][triangle] CreateCommittedResource(vertex) failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = sizeof(kTriangle);
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);

    return true;
}

bool SmokeTriangle12::CreateRootSignature(ID3D12Device* device)
{
    // Пустая root signature: параметров, CBV/SRV/UAV и сэмплеров нет.
    // Флаг ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT обязателен, иначе лист
    // с IASetVertexBuffers не пройдёт validation: по умолчанию IA
    // считается выключенным для root signature.
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 0;
    desc.pParameters = nullptr;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3D10Blob> signature;
    ComPtr<ID3D10Blob> error;

    HRESULT hr = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            dx12log::Write("[DX12][triangle] SerializeRootSignature failed, hr=0x%08X: %s",
                           static_cast<unsigned>(hr),
                           static_cast<const char*>(error->GetBufferPointer()));
        }
        else
        {
            dx12log::Write("[DX12][triangle] SerializeRootSignature failed, hr=0x%08X, no diagnostic",
                           static_cast<unsigned>(hr));
        }
        return false;
    }

    hr = device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][triangle] CreateRootSignature failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

bool SmokeTriangle12::CreatePipelineState(ID3D12Device* device)
{
    ComPtr<ID3D10Blob> vertexShader;
    ComPtr<ID3D10Blob> pixelShader;

    if (!CompileShader(kVertexShaderSrc, "main", "vs_5_0", &vertexShader))
        return false;

    if (!CompileShader(kPixelShaderSrc, "main", "ps_5_0", &pixelShader))
        return false;

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_rootSignature.Get();
    desc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    desc.VS.BytecodeLength = vertexShader->GetBufferSize();
    desc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
    desc.PS.BytecodeLength = pixelShader->GetBufferSize();
    desc.InputLayout.pInputElementDescs = inputLayout;
    desc.InputLayout.NumElements = 1;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // Формат совпадает с BACKBUFFER_FORMAT в CHW12.
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.NumRenderTargets = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;

    desc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // CullMode NONE исключает ошибки winding order на первом треугольнике.
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthClipEnable = TRUE;

    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][triangle] CreateGraphicsPipelineState failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

void SmokeTriangle12::Draw(ID3D12GraphicsCommandList* cmd)
{
    if (!m_created || !cmd)
        return;

    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pipelineState.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m_vertexBufferView);

    cmd->DrawInstanced(3, 1, 0, 0);
}

void SmokeTriangle12::Quarantine(
    ID3D12Resource*& outVertexBuffer,
    ID3D12Resource*& outUploadBuffer,
    ID3D12RootSignature*& outRootSignature,
    ID3D12PipelineState*& outPipelineState)
{
    // Detach() снимает владение без вызова Release: счётчик ссылок каждого
    // объекта не меняется. Деструктор SmokeTriangle12 после этого ничего не
    // освобождает, потому что ComPtr равны nullptr.
    outVertexBuffer  = m_vertexBuffer.Detach();
    outUploadBuffer  = m_uploadBuffer.Detach();
    outRootSignature = m_rootSignature.Detach();
    outPipelineState = m_pipelineState.Detach();
    m_vertexBufferView = {};

    // Флаги сбрасываются: после карантина компонент считается нерабочим.
    // Release() при этом станет no-op, потому что все ComPtr равны nullptr.
    m_created = false;
    m_copySubmitted = false;
}

void SmokeTriangle12::Release()
{
    // Проверяем сами объекты, а не m_created: при частично неуспешном Create
    // (например, root signature создана, а PSO упал) ресурсы уже существуют,
    // и ранний выход по флагу оставил бы их висеть до ReportLiveObjects.
    if (!m_pipelineState && !m_rootSignature && !m_uploadBuffer && !m_vertexBuffer)
        return;

    // Порядок: pipeline и root signature перед буфером. Освобождение
    // происходит в CHW12::Shutdown до ReportLiveObjects, поэтому отчёт
    // об утечках остаётся информативным для новых объектов.
    m_pipelineState.Reset();
    m_rootSignature.Reset();
    m_uploadBuffer.Reset();
    m_vertexBuffer.Reset();
    m_vertexBufferView = {};

    m_created = false;
    m_copySubmitted = false;
    dx12log::Write("[DX12][triangle] released");
}
