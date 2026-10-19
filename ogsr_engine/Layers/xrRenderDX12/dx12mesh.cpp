#include "stdafx.h"
#include "dx12mesh.h"
#include "dx12log.h"

#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>

// Вершина: POSITION (R32G32B32_FLOAT) + COLOR (R32G32B32A32_FLOAT).
// COLOR делает depth-тест наблюдаемым: перекрытие передних и задних
// граней визуально не доказуемо, а цвет позволяет отличить ближний куб от
// дальнего в readback (§4.4 ТЗ, D5).
struct Vertex
{
    float x, y, z;       // POSITION
    float r, g, b, a;    // COLOR
};

// Добавляет два треугольника грани куба: (v0,v1,v2) и (v0,v2,v3).
// Winding order намеренно не важен: PSO создаётся с CullMode = NONE.
static void AddFace(
    Vertex* dst,
    const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3)
{
    dst[0] = v0; dst[1] = v1; dst[2] = v2;
    dst[3] = v0; dst[4] = v2; dst[5] = v3;
}

// Один vertex buffer на 72 вершины (два куба по 36), без индексного буфера —
// индексы переносятся в F3.2. Раскладка:
//   [0, 36)  — ближний куб, около origin
//   [36, 72) — дальний куб, сдвинут по Z так, чтобы перекрываться на экране
//
// Мировые координаты записаны непосредственно в вершины (вариант A, §2.3 ТЗ).
// Матрица одна на кадр и от объекта не зависит, поэтому между двумя draw
// call CB не перезаписывается.
static void BuildCubeVertices(Vertex (&vertices)[72])
{
    // --- Ближний куб: центр (0,0,0), размер 2 (от -1 до 1 по каждой оси) ---

    // Передняя грань (z=1): красный
    const Vertex nearFront[] = {
        {-1.0f, -1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    };
    // Задняя грань (z=-1): зелёный
    const Vertex nearBack[] = {
        { 1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
    };
    // Левая грань (x=-1): синий
    const Vertex nearLeft[] = {
        {-1.0f, -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {-1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {-1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
    };
    // Правая грань (x=1): жёлтый
    const Vertex nearRight[] = {
        { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f,  1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
    };
    // Верхняя грань (y=1): пурпурный
    const Vertex nearTop[] = {
        {-1.0f, 1.0f,  1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        { 1.0f, 1.0f,  1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        { 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
    };
    // Нижняя грань (y=-1): голубой
    const Vertex nearBottom[] = {
        {-1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        { 1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        { 1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        {-1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    };

    AddFace(&vertices[0],  nearFront[0], nearFront[1], nearFront[2], nearFront[3]);
    AddFace(&vertices[6],  nearBack[0], nearBack[1], nearBack[2], nearBack[3]);
    AddFace(&vertices[12], nearLeft[0], nearLeft[1], nearLeft[2], nearLeft[3]);
    AddFace(&vertices[18], nearRight[0], nearRight[1], nearRight[2], nearRight[3]);
    AddFace(&vertices[24], nearTop[0], nearTop[1], nearTop[2], nearTop[3]);
    AddFace(&vertices[30], nearBottom[0], nearBottom[1], nearBottom[2], nearBottom[3]);

    // --- Дальний куб: центр (0,0,2), размер 4 (от -2 到 2 по x/y, 从 0 到 4 по z) ---

    // Передняя грань (z=0): синий
    const Vertex farFront[] = {
        {-2.0f, -2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        { 2.0f, -2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        { 2.0f,  2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {-2.0f,  2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
    };
    // Задняя грань (z=4): пурпурный
    const Vertex farBack[] = {
        { 2.0f, -2.0f, 4.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-2.0f, -2.0f, 4.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-2.0f,  2.0f, 4.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        { 2.0f,  2.0f, 4.0f, 1.0f, 0.0f, 1.0f, 1.0f},
    };
    // Левая грань (x=-2): зелёный
    const Vertex farLeft[] = {
        {-2.0f, -2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {-2.0f, -2.0f, 4.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {-2.0f,  2.0f, 4.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {-2.0f,  2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
    };
    // Правая грань (x=2): жёлтый
    const Vertex farRight[] = {
        { 2.0f, -2.0f, 4.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        { 2.0f, -2.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        { 2.0f,  2.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        { 2.0f,  2.0f, 4.0f, 1.0f, 1.0f, 0.0f, 1.0f},
    };
    // Верхняя грань (y=2): голубой
    const Vertex farTop[] = {
        {-2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        { 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        { 2.0f, 2.0f, 4.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        {-2.0f, 2.0f, 4.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    };
    // Нижняя грань (y=-2): красный
    const Vertex farBottom[] = {
        {-2.0f, -2.0f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        { 2.0f, -2.0f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        { 2.0f, -2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        {-2.0f, -2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    };

    AddFace(&vertices[36], farFront[0], farFront[1], farFront[2], farFront[3]);
    AddFace(&vertices[42], farBack[0], farBack[1], farBack[2], farBack[3]);
    AddFace(&vertices[48], farLeft[0], farLeft[1], farLeft[2], farLeft[3]);
    AddFace(&vertices[54], farRight[0], farRight[1], farRight[2], farRight[3]);
    AddFace(&vertices[60], farTop[0], farTop[1], farTop[2], farTop[3]);
    AddFace(&vertices[66], farBottom[0], farBottom[1], farBottom[2], farBottom[3]);
}

// Шейдеры держим строками в коде: отдельный .hlsl потребовал бы либо
// компиляции на этапе сборки, либо пути к файлу в рантайме. Для двух
// кубов строковый вариант проще и не добавляет зависимостей.
static const char* kVertexShaderSrc = R"(
struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

// ViewProjection приходит из constant buffer (register b0).
// mul(float4, float4x4) — вектор СЛЕВА, что согласовано с row-vector
// конвенцией DirectXMath и транспонированием на CPU (§6.3 ТЗ).
// Комбинация «transpose на CPU + mul(matrix, vector)» ЗАПРЕЩЕНА:
// она применит матрицу дважды транспонированно, то есть исходную
// viewProj в другом порядке. Рендер даст искажённую геометрию,
// validation промолчит.
cbuffer ViewProjectionCB : register(b0)
{
    float4x4 ViewProjection;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), ViewProjection);
    output.color = input.color;
    return output;
}
)";

static const char* kPixelShaderSrc = R"(
struct PSInput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_Target
{
    return input.color;
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
        "dx12mesh_inline",
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
            dx12log::Write("[DX12][mesh] shader compile failed (%s / %s), hr=0x%08X: %s",
                           entryPoint, target, static_cast<unsigned>(hr),
                           static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        else
        {
            dx12log::Write("[DX12][mesh] shader compile failed (%s / %s), hr=0x%08X, no diagnostic",
                           entryPoint, target, static_cast<unsigned>(hr));
        }
        return false;
    }

    return true;
}
}  // namespace

SmokeMesh12::~SmokeMesh12()
{
    Release();
}

bool SmokeMesh12::Create(
    ID3D12Device* device,
    ID3D12CommandQueue* copyQueue,
    ID3D12CommandAllocator* copyAllocator,
    ID3D12GraphicsCommandList* copyCommandList)
{
    if (m_created)
    {
        dx12log::Write("[DX12][mesh] Create called twice, refusing");
        return false;
    }

    if (!device || !copyQueue || !copyAllocator || !copyCommandList)
    {
        dx12log::Write("[DX12][mesh] Create refused: null dependency");
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
    // меша и RenderFrame столкнулись бы на одном fence.
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // Размер — все 72 вершины. Вершинный буфер при resize не пересоздаётся.
    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = sizeof(Vertex) * kVertexCount;
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
        dx12log::Write("[DX12][mesh] CreateCommittedResource(upload) failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    hr = m_uploadBuffer->Map(0, &readRange, &mapped);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][mesh] upload buffer Map failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        m_uploadBuffer.Reset();
        return false;
    }

    // Копируем вершины в upload buffer. BuildCubeVertices заполняет массив
    // по ссылке, поэтому memcpy выполняется один раз.
    Vertex vertices[kVertexCount];
    BuildCubeVertices(vertices);
    // Bounds из того же массива, который копируется на GPU.
    for (uint32_t i=0;i<2;++i) {
        auto& o=m_objects[i]; o.id=i+1; o.firstVertex=i*36; o.vertexCount=36;
        DirectX::XMStoreFloat4x4(&o.world,DirectX::XMMatrixIdentity());
        o.minimum=o.maximum={vertices[i*36].x,vertices[i*36].y,vertices[i*36].z};
        for (uint32_t j=i*36;j<(i+1)*36;++j) {
            const auto& v=vertices[j];
            o.minimum.x=(std::min)(o.minimum.x,v.x); o.maximum.x=(std::max)(o.maximum.x,v.x);
            o.minimum.y=(std::min)(o.minimum.y,v.y); o.maximum.y=(std::max)(o.maximum.y,v.y);
            o.minimum.z=(std::min)(o.minimum.z,v.z); o.maximum.z=(std::max)(o.maximum.z,v.z);
        }
    }
    memcpy(mapped, vertices, sizeof(vertices));
    m_uploadBuffer->Unmap(0, nullptr);

    // Командный лист для копии берём у вызывающего: CHW12::CreateFrameResources
    // создаёт закрытые листы, а Reset() в них был бы первым обращением из
    // RenderFrame. Свой отдельный лист потребовал бы отдельного аллокатора.
    hr = copyCommandList->Reset(copyAllocator, nullptr);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][mesh] copy command list Reset failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        m_uploadBuffer.Reset();
        return false;
    }

    copyCommandList->CopyBufferRegion(
        m_vertexBuffer.Get(), 0, m_uploadBuffer.Get(), 0, sizeof(Vertex) * kVertexCount);

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
        dx12log::Write("[DX12][mesh] copy command list Close failed, hr=0x%08X",
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
    dx12log::Write("[DX12][mesh] vertex upload submitted, awaiting GPU drain");
    return true;
}

// Вызывается из CHW12::Initialize после Create и успешного WaitForGpu.
// GPU завершил копирование, поэтому upload buffer можно освободить.
void SmokeMesh12::OnUploadComplete()
{
    if (!m_copySubmitted)
        return;

    m_uploadBuffer.Reset();
    m_copySubmitted = false;
    m_created = true;

    dx12log::Write("[DX12][mesh] created: %u vertices (2 cubes), root signature and PSO ready",
                   kVertexCount);
}

bool SmokeMesh12::CreateVertexBuffer(ID3D12Device* device)
{
    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeof(Vertex) * kVertexCount;
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
        dx12log::Write("[DX12][mesh] CreateCommittedResource(vertex) failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = sizeof(Vertex) * kVertexCount;
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);

    return true;
}

bool SmokeMesh12::CreateRootSignature(ID3D12Device* device)
{
    // Один root parameter: CBV, register 0, ShaderVisibility_ALL.
    // Дескриптор не нужен: адрес передаётся через
    // SetGraphicsRootConstantBufferView. Убирает CBV_SRV_UAV heap,
    // шейдер-visible heap и его пересоздание при resize.
    //
    // Флаг ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT обязателен, иначе лист
    // с IASetVertexBuffers не пройдёт validation: по умолчанию IA
    // считается выключенным для root signature.
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &rootParam;
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
            dx12log::Write("[DX12][mesh] SerializeRootSignature failed, hr=0x%08X: %s",
                           static_cast<unsigned>(hr),
                           static_cast<const char*>(error->GetBufferPointer()));
        }
        else
        {
            dx12log::Write("[DX12][mesh] SerializeRootSignature failed, hr=0x%08X, no diagnostic",
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
        dx12log::Write("[DX12][mesh] CreateRootSignature failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

bool SmokeMesh12::CreatePipelineState(ID3D12Device* device)
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

    // Формат совпадает с BACKBUFFER_FORMAT в CHW12.
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.NumRenderTargets = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    // DSVFormat обязан совпадать с форматом depth buffer (D32_FLOAT).
    // Расхождение даёт validation error, то есть детектируемо.
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.StencilEnable = FALSE;

    desc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // CullMode NONE сохраняется: F3.1 валидирует глубину, а не winding order.
    // Winding проверяется на реальном формате вершин OGSR в F3.2.
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthClipEnable = TRUE;

    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12][mesh] CreateGraphicsPipelineState failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

void SmokeMesh12::Draw(
    ID3D12GraphicsCommandList* cmd,
    D3D12_GPU_VIRTUAL_ADDRESS cbVA,
    UINT firstVertex,
    UINT vertexCount)
{
    if (!m_created || !cmd)
        return;

    // Валидация диапазона обязательна: выход за пределы буфера был бы GPU
    // page fault. GBV это не ловит: адрес выровнен и валиден, но чтение
    // за пределами буфера неопределённо.
    if (firstVertex + vertexCount > kVertexCount)
    {
        dx12log::Write("[DX12][mesh] Draw refused: firstVertex=%u + vertexCount=%u > %u",
                       firstVertex, vertexCount, kVertexCount);
        return;
    }

    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pipelineState.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    cmd->SetGraphicsRootConstantBufferView(0, cbVA);
    cmd->DrawInstanced(vertexCount, 1, firstVertex, 0);
}

void SmokeMesh12::Quarantine(
    ID3D12Resource*& outVertexBuffer,
    ID3D12Resource*& outUploadBuffer,
    ID3D12RootSignature*& outRootSignature,
    ID3D12PipelineState*& outPipelineState)
{
    // Detach() снимает владение без вызова Release: счётчик ссылок каждого
    // объекта не меняется. Деструктор SmokeMesh12 после этого ничего не
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

void SmokeMesh12::Release()
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
    dx12log::Write("[DX12][mesh] released");
}
