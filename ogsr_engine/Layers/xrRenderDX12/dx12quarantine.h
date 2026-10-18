#pragma once

// F3.0: аварийное хранение GPU-ресурсов при неизвестном состоянии GPU.
//
// ОБЛАСТЬ ПРИМЕНЕНИЯ — строго один standalone экземпляр Smoke. Для будущего
// игрового renderer'а эта политика НЕ применима: удержание ресурсов до
// завершения процесса означает удержание видеопамяти до перезапуска, что
// недопустимо. Там потребуется решение на уровне движка (fail-stop с
// завершением процесса либо полное пересоздание устройства).
//
// НАМЕРЕННОЕ УДЕРЖАНИЕ РЕСУРСОВ. Объекты, переданные сюда, не освобождаются
// до завершения процесса. Это не утечка по неосторожности, а единственный
// корректный способ сохранить валидность памяти для GPU, который может ещё
// выполнять команды. Microsoft требует хранить ресурсы до подтверждённого
// завершения их использования GPU; когда завершение подтвердить невозможно,
// единственной безопасной альтернативой является не освобождать их вовсе.
//
// ХРАНИЛИЩЕ БЕЗ HEAP-ВЫДЕЛЕНИЯ. Storage — статический объект с process
// lifetime, поэтому аварийный путь не выполняет аллокаций. Отсутствие
// аллокации в аварийном пути принципиально: при неизвестном состоянии
// системы полагаться на успех new нельзя.
//
// ОТСУТСТВИЕ АВТОМАТИЧЕСКОГО ОСВОБОЖДЕНИЯ. Деструктор этого типа не
// объявлен и не генерируется компилятором: все поля — тривиальные типы
// (сырые указатели и HANDLE). Ни Release(), ни CloseHandle() автоматически
// не вызывается ни при каких обстоятельствах.
//
// ВЛАДЕНИЕ. COM-ссылки передаются через ComPtr::Detach(), то есть счётчик
// ссылок не меняется и AddRef() не используется. Деструкторы CHW12 и
// SmokeTriangle12 после Detach() владения не имеют (поля равны nullptr) и
// эти объекты не трогают. HANDLE передаётся присваиванием с обнулением
// исходного поля.
//
// ПОВТОРНОЕ ЗАНЯТИЕ ЗАПРЕЩЕНО. Флаг occupied устанавливается при передаче
// владения и не сбрасывается. Вторая попытка карантина отклоняется: иначе
// указатели первой партии были бы потеряны безвозвратно.

#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// IDXGISwapChain4 объявлен в dxgi1_5.h. Заголовок dxgi1_6.h, который
// использует CHW12, цепочку включений к нему не строит, поэтому без явного
// включения dxgi1_5.h компилятор не знает этот тип. Проверено по SDK
// 10.0.26100.0: объявление находится только в dxgi1_5.h.
#include <dxgi1_5.h>
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Количество слотов кадра. Совпадает с CHW12::FRAME_COUNT. Значение
// продублировано намеренно: этот заголовок не зависит от CHW12.h, чтобы
// не тянуть за собой весь bootstrap ради одной константы.
constexpr uint32_t kQuarantineFrameCount = 3;

struct GpuQuarantine
{
    // Слот кадра. Собственный тип, а не CHW12::FrameResource: тот является
    // приватным вложенным типом CHW12 и недоступен отсюда по правилам языка.
    struct Slot
    {
        ID3D12Resource* renderTarget = nullptr;
        ID3D12CommandAllocator* commandAllocator = nullptr;
        ID3D12GraphicsCommandList* commandList = nullptr;
        uint64_t fenceValue = 0;
    };

    // Карантин занят. Повторное занятие запрещено.
    bool occupied = false;

    // Устройство, очередь, swapchain и хип дескрипторов.
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* commandQueue = nullptr;
    IDXGISwapChain4* swapChain = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;

    // Backbuffers и средства записи команд.
    Slot slots[kQuarantineFrameCount];

    // Ресурсы треугольника. Владение передаёт SmokeTriangle12::Quarantine().
    ID3D12Resource* vertexBuffer = nullptr;
    ID3D12Resource* uploadBuffer = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;

    // F3.1: depth buffer и DSV heap. Depth buffer — чистый GPU-ресурс,
    // CPU в него не пишет никогда, но при неизвестном GPU completion
    // он может ещё читаться, поэтому передаётся карантину.
    ID3D12Resource* depthBuffer = nullptr;
    ID3D12DescriptorHeap* dsvHeap = nullptr;

    // F3.1: per-frame constant buffers. Владение передаётся через Detach().
    // CPU-указатели mapped обнуляются в момент передачи (§3.3 ТЗ): после
    // Detach объект принадлежит карантину, и оставшийся валидным mapped
    // позволял бы писать в upload heap, который потенциально ещё читает GPU.
    ID3D12Resource* constantBuffers[kQuarantineFrameCount] = {};

    // F3.1: readback buffer (только тестовый режим). mappedReadback
    // обнуляется там же — readback-буфер тоже Map'ится, и запись в него
    // после передачи владения была бы ошибкой того же класса.
    ID3D12Resource* readbackBuffer = nullptr;

    // Синхронизация. Fence удерживается по той же причине, что и остальные
    // ресурсы: при таймауте ожидания runtime сохраняет регистрацию
    // SetEventOnCompletion и может записать в event позже, поэтому и fence,
    // и event должны оставаться валидными. Таймаут означает лишь «CPU
    // перестал ждать», но не «runtime перестал использовать регистрацию».
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
};

// Хранилище с process lifetime. Объявление определения — в dx12quarantine.cpp.
// Использование статического объекта, а не new: аварийный путь не должен
// выполнять heap-аллокаций.
extern GpuQuarantine g_gpuQuarantine;
