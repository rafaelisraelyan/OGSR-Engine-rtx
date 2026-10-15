#pragma once

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// F3.0: минимальный вспомогательный компонент для первого DX12-треугольника.
// Вынесен из CHW12, чтобы CHW12 оставался владельцем только device/queue/
// swapchain/fence, а владение геометрией и pipeline не смешивалось с ними.
//
// Это bootstrap-компонент, а не будущая публичная архитектура CRender12:
// никаких абстракций материалов, нескольких PSO или менеджера ресурсов.
//
// Соглашения по lifetime:
//  - Create() вызывается из CHW12::Initialize после CreateSynchronization,
//    когда command queue и fence уже существуют;
//  - Draw() записывает команды в переданный command list и не хранит
//    состояния allocator'а, поэтому пересоздание листов при resize
//    его не затрагивает;
//  - Release() вызывается из CHW12::Shutdown до ReportLiveObjects.
class SmokeTriangle12
{
public:
    SmokeTriangle12() = default;
    ~SmokeTriangle12();

    SmokeTriangle12(const SmokeTriangle12&) = delete;
    SmokeTriangle12& operator=(const SmokeTriangle12&) = delete;

    // device и queue берутся только на время Create: компонент не хранит их.
    // Ожидание GPU после загрузки вершин выполняет вызывающий код через
    // WaitForGpu: fence остаётся во владении CHW12, иначе значения сигналов
    // треугольника и RenderFrame столкнулись бы на одном fence.
    bool Create(
        ID3D12Device* device,
        ID3D12CommandQueue* copyQueue,
        ID3D12CommandAllocator* copyAllocator,
        ID3D12GraphicsCommandList* copyCommandList);

    // Вызывается после успешного WaitForGpu: GPU завершил копирование,
    // и upload buffer можно освободить. До этого вызова Draw() не рисует.
    void OnUploadComplete();

    // Записывает draw-команды в переданный command list. Вызывается из
    // CHW12::RenderFrame между ClearRenderTargetView и барьером в PRESENT.
    void Draw(ID3D12GraphicsCommandList* cmd);

    // Передаёт владение ресурсами карантину. Владение передаётся через
    // Detach(), поэтому счётчики ссылок не меняются и AddRef не нужен.
    // После вызова деструктор SmokeTriangle12 эти объекты не освобождает:
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

    bool IsCreated() const { return m_created; }

private:
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
