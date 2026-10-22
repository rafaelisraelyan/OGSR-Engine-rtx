#pragma once

#include <d3d12.h>
#include "dx12renderprep.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// F3.1: компонент геометрии — два куба с мировыми координатами, записанными
// непосредственно в вершины (вариант A, §2.3 ТЗ). Depth-tested, с одной
// общей ViewProjection на кадр.
//
// F3.5.6A: opt-in Transforms mode использует одну local-space геометрию;
// World matrix каждого объекта приходит из per-object CB. Legacy остаётся
// default и сохраняет прежний world-baked путь.
//
// Переименован из SmokeTriangle12: содержание изменилось принципиально
// (depth, MVP, куб), и имя «Triangle» на кубе сделало бы код лгущим.
// Структура Create/OnUploadComplete/Draw/Quarantine/Release сохранена.
//
// Соглашения по lifetime:
//  - Create() вызывается из CHW12::Initialize после CreateSynchronization,
//    когда command queue и fence уже существуют;
//  - Draw() записывает команды в переданный command list и не хранит
//    состояния allocator'а, поэтому пересоздание листов при resize
//    его не затрагивает;
//  - Release() вызывается из CHW12::Shutdown до ReportLiveObjects.
//
// Draw НЕ пишет в constant buffer: матрица одна на кадр и от объекта не
// зависит (§2.3), поэтому между двумя draw call CB не перезаписывается.
// GBV это не ловит: адрес выровнен и валиден, но второй куб получил бы
// матрицу первого, а первый — потенциально мусор.
enum class MeshSceneMode12 : uint8_t
{
    Legacy,
    Transforms,
    Geometry,
    XrayAsset
};

class SmokeMesh12
{
public:
    static constexpr uint32_t kVertexCount = 72; // 2 куба × 36 вершин

    SmokeMesh12() = default;
    ~SmokeMesh12();

    SmokeMesh12(const SmokeMesh12&) = delete;
    SmokeMesh12& operator=(const SmokeMesh12&) = delete;

    // device и queue берутся только на время Create: компонент не хранит
    // их. Ожидание GPU после загрузки вершин выполняет вызывающий код
    // через WaitForGpu: fence остаётся во владении CHW12, иначе значения
    // сигналов меша и RenderFrame столкнулись бы на одном fence.
    bool Create(
        ID3D12Device* device,
        ID3D12CommandQueue* copyQueue,
        ID3D12CommandAllocator* copyAllocator,
        ID3D12GraphicsCommandList* copyCommandList,
        MeshSceneMode12 mode);

    // Вызывается после успешного WaitForGpu: GPU завершил копирование,
    // и upload buffer можно освободить. До этого вызова Draw() не рисует.
    void OnUploadComplete();

    // Записывает draw-команды в переданный command list. Вызывается из
    // CHW12::RenderFrame между ClearRenderTargetView и барьером в PRESENT.
    //
    // cbVA — адрес constant buffer текущего слота (передаётся, а не
    // пишется: Draw не трогает CB). Одна ViewProjection на кадр, оба
    // draw call используют один адрес.
    //
    // Валидация диапазона обязательна: firstVertex + vertexCount <= 72,
    // иначе false без записи команд. Диагностика остаётся на owner thread. Выход за пределы буфера
    // был бы GPU page fault.
    // Read-only mesh: параллельные вызовы допустимы с разными command lists,
    // если Create/Release/Quarantine не перекрываются с записью.
    bool Draw(
        ID3D12GraphicsCommandList* cmd,
        D3D12_GPU_VIRTUAL_ADDRESS cbVA,
        UINT firstVertex,
        UINT vertexCount) const;

    // F3.5.6A: per-object draw path. frameCBVA — b0 ViewProjection,
    // objectCBVA — b1 World/Tint текущего packet. Каждый вызов выставляет
    // root signature, PSO, topology, VB и оба CBV, поэтому state не
    // наследуется между command lists.
    bool DrawTransformed(
        ID3D12GraphicsCommandList* cmd,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBVA,
        D3D12_GPU_VIRTUAL_ADDRESS objectCBVA,
        UINT firstVertex,
        UINT vertexCount) const;

    // Передаёт владение ресурсами карантину. Владение передаётся через
    // Detach(), поэтому счётчики ссылок не меняются и AddRef не нужен.
    // После вызова деструктор SmokeMesh12 эти объекты не освобождает:
    // соответствующие ComPtr равны nullptr, и Release() становится no-op.
    //
    // Вызывается только из CHW12 при неподтверждённом завершении GPU-работы.
    // В штатном пути не используется.
    void Quarantine(
        ID3D12Resource*& outVertexBuffer,
        ID3D12Resource*& outUploadBuffer,
        ID3D12RootSignature*& outRootSignature,
        ID3D12PipelineState*& outPipelineState);

    void Release();

    const std::array<renderprep12::Object12, 2>& Objects() const { return m_objects; }

    bool IsCreated() const { return m_created; }
    MeshSceneMode12 Mode() const { return m_mode; }
    uint32_t VertexCount() const { return m_vertexCount; }

private:
    std::array<renderprep12::Object12, 2> m_objects{};
    MeshSceneMode12 m_mode = MeshSceneMode12::Legacy;
    uint32_t m_vertexCount = kVertexCount;
    bool CreateVertexBuffer(ID3D12Device* device);
    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelineState(ID3D12Device* device);

    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_uploadBuffer;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};

    // Команды копии отправлены в очередь, но GPU их ещё не выполнил.
    bool m_copySubmitted = false;

    // Загрузка завершена, Draw() готов рисовать.
    bool m_created = false;
};
