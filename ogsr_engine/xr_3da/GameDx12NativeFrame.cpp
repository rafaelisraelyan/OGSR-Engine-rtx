#include "stdafx.h"
#include "GameDx12NativeFrame.h"
#include "GameDx12Water.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
constexpr DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr DWORD fenceTimeoutMs = 10000;
constexpr uint64_t removedFenceValue = UINT64_MAX;
constexpr uint32_t captureFrames = 30;
constexpr uint32_t captureMaxWidth = 1920;
constexpr uint32_t captureMaxHeight = 1080;
constexpr uint32_t captureSceneStart = 120;

D3D12_RESOURCE_BARRIER ColorBarrier(ID3D12Resource* resource,
                                   D3D12_RESOURCE_STATES before,
                                   D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          before, after};
    return barrier;
}
}

GameDx12NativeFrame12::~GameDx12NativeFrame12()
{
    Release();
}

HRESULT GameDx12NativeFrame12::Initialize(HWND window, IDXGIFactory2& factory,
                                         const Owner& owner)
{
    RECT rect{};
    if (m_swapchain || m_unknown || !window || !GetClientRect(window, &rect) ||
        rect.right <= rect.left || rect.bottom <= rect.top)
        return E_INVALIDARG;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(rect.right - rect.left);
    desc.Height = static_cast<UINT>(rect.bottom - rect.top);
    desc.Format = colorFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kFrameCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
    HRESULT hr = factory.CreateSwapChainForHwnd(&owner.queue, window, &desc, nullptr,
                                                nullptr, swapchain.GetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = factory.MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr))
        return hr;
    hr = swapchain.As(&m_swapchain);
    if (FAILED(hr))
        return hr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};
    descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptors.NumDescriptors = kFrameCount;
    hr = owner.device.CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(m_rtv.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    m_rtvStride = owner.device.GetDescriptorHandleIncrementSize(descriptors.Type);
    descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    descriptors.NumDescriptors = 2;
    m_dsvStride = owner.device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    hr = owner.device.CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(m_dsv.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    const char* commandLists = std::getenv("OGSR_DX12_GAME_COMMAND_LISTS");
    m_parallelShadowLists = commandLists && std::strcmp(commandLists, "parallel") == 0;
    m_separateShadowLists = m_parallelShadowLists || (commandLists && std::strcmp(commandLists, "multi") == 0);
    for (auto& slot : m_slots)
    {
        hr = CreateList(owner.device, slot.allocator, slot.list);
        if (FAILED(hr))
            return hr;
        if (!m_separateShadowLists)
            continue;
        hr = CreateList(owner.device, slot.colorPass.allocator, slot.colorPass.list);
        if (FAILED(hr))
            return hr;
    }
    hr = CreateTargets(owner.device, desc.Width, desc.Height);
    if (FAILED(hr))
        return hr;

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
    const DWORD directory = GetEnvironmentVariableW(L"OGSR_DX12_NATIVE_CAPTURE_DIR",
                                                  m_capturePath, MAX_PATH);
    if (directory >= MAX_PATH ||
        (directory != 0 && wcscat_s(m_capturePath, L"\\native-frames.rgba") != 0))
    {
        m_capturePath[0] = L'\0';
        Msg("ph_dx12_capture: invalid capture path");
    }
#endif

    const char* measure = std::getenv("OGSR_DX12_GAME_METRICS");
    const char* sceneLog = std::getenv("OGSR_DX12_GAME_SCENE_LOG");
    m_measureFrame = (measure != nullptr && std::strcmp(measure, "1") == 0) ||
        (sceneLog != nullptr && std::strcmp(sceneLog, "1") == 0);
    m_timestampFrequency = 0;
    if (m_measureFrame && FAILED(owner.queue.GetTimestampFrequency(&m_timestampFrequency)))
        m_timestampFrequency = 0;
    return S_OK;
}

D3D12_CPU_DESCRIPTOR_HANDLE GameDx12NativeFrame12::Rtv(uint32_t index) const noexcept
{
    auto handle = m_rtv->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_rtvStride;
    return handle;
}

HRESULT GameDx12NativeFrame12::CreateTargets(ID3D12Device& device,
                                            uint32_t width, uint32_t height)
{
    for (uint32_t index = 0; index < kFrameCount; ++index)
    {
        auto& slot = m_slots[index];
        const HRESULT hr = m_swapchain->GetBuffer(index, IID_PPV_ARGS(slot.color.GetAddressOf()));
        if (FAILED(hr))
            return hr;
        device.CreateRenderTargetView(slot.color.Get(), nullptr, Rtv(index));
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC depth{};
    depth.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depth.Width = width;
    depth.Height = height;
    depth.DepthOrArraySize = 1;
    depth.MipLevels = 1;
    depth.Format = DXGI_FORMAT_R32_TYPELESS;
    depth.SampleDesc.Count = 1;
    depth.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = depthFormat;
    clear.DepthStencil.Depth = 1.0f;
    const HRESULT hr = device.CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &depth, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear, IID_PPV_ARGS(m_depth.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};
    depthView.Format = depthFormat;
    depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    auto dsv = m_dsv->GetCPUDescriptorHandleForHeapStart();
    device.CreateDepthStencilView(m_depth.Get(), &depthView, dsv);
    dsv.ptr += m_dsvStride;
    depthView.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    device.CreateDepthStencilView(m_depth.Get(), &depthView, dsv);

    if (gamewater12::WaterEnabled() || gamewater12::PuddlesEnabled())
    {
        auto colorCopy = depth;
        colorCopy.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        colorCopy.Flags = D3D12_RESOURCE_FLAG_NONE;
        for (auto& slot : m_slots)
        {
            const HRESULT copyHr = device.CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &colorCopy,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(slot.opaqueColor.GetAddressOf()));
            if (FAILED(copyHr))
                Msg("ph_dx12_f43e: water scene copy unavailable hr=0x%08X", static_cast<unsigned>(copyHr));
        }
    }
    m_width = width;
    m_height = height;
    return S_OK;
}

HRESULT GameDx12NativeFrame12::Wait(const Owner& owner, uint64_t value) noexcept
{
    if (value == 0)
        return S_OK;
    HRESULT hr = S_OK;
    if (owner.fence.GetCompletedValue() < value)
    {
        hr = owner.fence.SetEventOnCompletion(value, owner.event);
        if (SUCCEEDED(hr) && WaitForSingleObject(owner.event, fenceTimeoutMs) != WAIT_OBJECT_0)
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (SUCCEEDED(hr))
        hr = owner.device.GetDeviceRemovedReason();
    const uint64_t completed = owner.fence.GetCompletedValue();
    if (SUCCEEDED(hr) && (completed == removedFenceValue || completed < value))
        hr = E_FAIL;
    if (FAILED(hr))
        m_unknown = true;
    return hr;
}

HRESULT GameDx12NativeFrame12::Drain(const Owner& owner) noexcept
{
    if (m_unknown || m_recording)
        return E_FAIL;
    if (!m_submitted)
        return S_OK;
    if (owner.fenceValue >= removedFenceValue - 1)
    {
        m_unknown = true;
        return E_FAIL;
    }
    const uint64_t barrier = owner.fenceValue + 1;
    const HRESULT hr = owner.queue.Signal(&owner.fence, barrier);
    if (FAILED(hr))
    {
        m_unknown = true;
        return hr;
    }
    owner.fenceValue = barrier;
    const HRESULT complete = Wait(owner, barrier);
    if (SUCCEEDED(complete))
    {
        m_submitted = false;
        for (auto& slot : m_slots)
            slot.fence = 0;
    }
    return complete;
}

HRESULT GameDx12NativeFrame12::Resize(const Owner& owner, uint32_t width, uint32_t height)
{
    HRESULT hr = Drain(owner);
    if (FAILED(hr))
        return hr;
    if (m_captureReadback)
    {
        Msg("ph_dx12_capture: cancelled on viewport resize after %u frames", m_captureCount);
        m_capturePath[0] = L'\0';
        m_captureReadback.Reset();
    }
    m_depth.Reset();
    for (auto& slot : m_slots)
    {
        slot.color.Reset();
        slot.opaqueColor.Reset();
    }
    hr = m_swapchain->ResizeBuffers(kFrameCount, width, height, colorFormat, 0);
    if (FAILED(hr))
        return hr;
    return CreateTargets(owner.device, width, height);
}

HRESULT GameDx12NativeFrame12::Recording::StageShadowPages(game_shadow12::ShadowDescriptors& descriptors,
    game_shadow12::ShadowPageResources& pages, game_shadow12::ShadowAddressPlan& addresses,
    game_shadow12::ShadowDescriptors::Binding& binding) const
{
    if (!frame || !frame->m_recording || !frame->m_recordingFence ||
        slot != frame->m_slot || frame->m_pendingShadowDescriptors)
        return E_UNEXPECTED;
    const auto hr = descriptors.Stage(*frame->m_recordingFence, slot, pages, addresses, binding);
    if (SUCCEEDED(hr))
        frame->m_pendingShadowDescriptors = &descriptors;
    return hr;
}

HRESULT GameDx12NativeFrame12::CreateList(ID3D12Device& device, Ptr<ID3D12CommandAllocator>& allocator,
    Ptr<ID3D12GraphicsCommandList>& list) noexcept
{
    auto hr = device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf()));
    if (SUCCEEDED(hr))
        hr = device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
            IID_PPV_ARGS(list.GetAddressOf()));
    return SUCCEEDED(hr) ? list->Close() : hr;
}

HRESULT GameDx12NativeFrame12::Begin(HWND window, const Owner& owner, Recording& recording, uint32_t timingViews)
{
    recording = {};
    m_lastGpuTiming.present = 0;
    m_lastGpuTiming.milliseconds = m_lastGpuTiming.sceneMilliseconds = m_lastGpuTiming.shadowMilliseconds = 0;
    m_lastGpuTiming.views.clear();
    if (!m_swapchain || !m_depth || m_unknown || m_recording)
        return E_FAIL;
    RECT rect{};
    if (IsIconic(window) || !GetClientRect(window, &rect) ||
        rect.right <= rect.left || rect.bottom <= rect.top)
        return S_FALSE;
    const auto width = static_cast<uint32_t>(rect.right - rect.left);
    const auto height = static_cast<uint32_t>(rect.bottom - rect.top);
    if (width != m_width || height != m_height)
    {
        const HRESULT hr = Resize(owner, width, height);
        if (FAILED(hr))
            return hr;
    }

    m_slot = m_swapchain->GetCurrentBackBufferIndex();
    if (m_slot >= kFrameCount)
        return E_FAIL;
    auto& slot = m_slots[m_slot];
    using Clock = std::chrono::steady_clock;
    const auto waitStart = m_measureFrame ? Clock::now() : Clock::time_point{};
    HRESULT hr = Wait(owner, slot.fence);
    if (FAILED(hr))
        return hr;
    const double slotFenceWaitMs = m_measureFrame ?
        std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count() : 0.0;
    if (slot.timingQueries && slot.timingPresent)
    {
        const SIZE_T bytes = (4 + 2 * slot.timingViews.size()) * sizeof(uint64_t);
        const D3D12_RANGE range{0, bytes};
        void* mapped = nullptr;
        try { m_lastGpuTiming.views.reserve(slot.timingViews.size()); }
        catch (const std::exception&) { return E_OUTOFMEMORY; }
        if (SUCCEEDED(slot.timingReadback->Map(0, &range, &mapped)))
        {
            const auto* ticks = static_cast<const uint64_t*>(mapped);
            if (ticks[1] >= ticks[0])
            {
                m_lastGpuTiming.present = slot.timingPresent;
                const double tickMs = 1000.0 / static_cast<double>(m_timestampFrequency);
                m_lastGpuTiming.milliseconds = tickMs * static_cast<double>(ticks[1] - ticks[0]);
                if (slot.timingScene && ticks[3] >= ticks[2])
                    m_lastGpuTiming.sceneMilliseconds = tickMs * static_cast<double>(ticks[3] - ticks[2]);
                uint64_t firstShadow = UINT64_MAX, lastShadow = 0;
                for (size_t view = 0; view < slot.timingViews.size(); ++view)
                {
                    const auto first = ticks[4 + 2 * view], last = ticks[5 + 2 * view];
                    if (last < first)
                        continue;
                    firstShadow = (std::min)(firstShadow, first);
                    lastShadow = (std::max)(lastShadow, last);
                    m_lastGpuTiming.views.push_back({slot.timingViews[view], tickMs * static_cast<double>(last - first)});
                }
                if (!m_lastGpuTiming.views.empty())
                    m_lastGpuTiming.shadowMilliseconds = tickMs * static_cast<double>(lastShadow - firstShadow);
            }
            const D3D12_RANGE noWrite{};
            slot.timingReadback->Unmap(0, &noWrite);
        }
        slot.timingPresent = 0;
    }
    slot.timingViews.clear();
    if (m_timestampFrequency && (!slot.timingQueries || timingViews > slot.timingCapacity))
    {
        if (timingViews > (UINT32_MAX - 4) / 2)
            return E_INVALIDARG;
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query.Count = 4 + 2 * timingViews;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC readback{};
        readback.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readback.Width = static_cast<uint64_t>(query.Count) * sizeof(uint64_t);
        readback.Height = 1;
        readback.DepthOrArraySize = 1;
        readback.MipLevels = 1;
        readback.SampleDesc.Count = 1;
        readback.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Ptr<ID3D12QueryHeap> queries;
        Ptr<ID3D12Resource> buffer;
        if (SUCCEEDED(owner.device.CreateQueryHeap(&query, IID_PPV_ARGS(queries.GetAddressOf()))) &&
            SUCCEEDED(owner.device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readback,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(buffer.GetAddressOf()))))
        {
            slot.timingQueries = std::move(queries);
            slot.timingReadback = std::move(buffer);
            slot.timingCapacity = timingViews;
        }
        else
        {
            slot.timingQueries.Reset();
            slot.timingReadback.Reset();
            Msg("ph_dx12_native: GPU timestamps unavailable");
        }
    }
    hr = slot.allocator->Reset();
    if (FAILED(hr))
        return hr;
    hr = slot.list->Reset(slot.allocator.Get(), nullptr);
    if (FAILED(hr))
        return hr;
    m_recordingFence = &owner.fence;
    m_recording = true;
    m_activeList = slot.list.Get();
    m_shadowCount = 0;

    m_timingScene = false;
    if (slot.timingQueries)
        slot.list->EndQuery(slot.timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);

    const auto barrier = ColorBarrier(slot.color.Get(), D3D12_RESOURCE_STATE_PRESENT,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
    slot.list->ResourceBarrier(1, &barrier);
    const auto rtv = Rtv(m_slot);
    const auto dsv = m_dsv->GetCPUDescriptorHandleForHeapStart();
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width),
                                 static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT area{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    // F4.1: статическая сцена рисуется без скайбокса, поэтому цвет очистки
    // задаёт «небо» кадра. OGSR_DX12_CLEAR позволяет переопределить его.
    float clear[4] = {0.42f, 0.50f, 0.62f, 1.0f};
    if (const char* clearSpec = std::getenv("OGSR_DX12_CLEAR"))
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if (std::sscanf(clearSpec, "%f,%f,%f", &r, &g, &b) == 3)
        {
            clear[0] = r;
            clear[1] = g;
            clear[2] = b;
        }
    }
    slot.list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    slot.list->RSSetViewports(1, &viewport);
    slot.list->RSSetScissorRects(1, &area);
    slot.list->ClearRenderTargetView(rtv, clear, 0, nullptr);
    slot.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    recording = {slot.list.Get(), m_slot, width, height, rtv, dsv,
                 {dsv.ptr + m_dsvStride}, slot.color.Get(), m_depth.Get(),
                 slot.opaqueColor.Get(), &owner.device, slotFenceWaitMs, this};
    return S_OK;
}

HRESULT GameDx12NativeFrame12::OpenList(AuxiliaryList& pair) noexcept
{
    if (pair.open || !pair.list)
        return E_UNEXPECTED;
    pair.recorded = false;
    HRESULT hr = pair.allocator->Reset();
    if (SUCCEEDED(hr))
        hr = pair.list->Reset(pair.allocator.Get(), nullptr);
    pair.open = SUCCEEDED(hr);
    return hr;
}

HRESULT GameDx12NativeFrame12::CloseList(AuxiliaryList& pair) noexcept
{
    if (!pair.open)
        return E_UNEXPECTED;
    pair.open = false;
    const HRESULT hr = pair.list->Close();
    pair.recorded = SUCCEEDED(hr);
    return hr;
}

bool GameDx12NativeFrame12::Recording::UsesShadowLists() const noexcept
{
    return frame && frame->m_separateShadowLists;
}

bool GameDx12NativeFrame12::Recording::UsesParallelShadowLists() const noexcept
{
    return frame && frame->m_parallelShadowLists;
}

HRESULT GameDx12NativeFrame12::Recording::OpenShadowList(uint32_t index,
    ID3D12GraphicsCommandList*& result) const noexcept
{
    result = nullptr;
    if (!UsesShadowLists() || !frame->m_recording || slot != frame->m_slot ||
        index != frame->m_shadowCount || index >= UINT32_MAX - 2 ||
        frame->m_activeList != frame->m_slots[slot].list.Get())
        return E_UNEXPECTED;
    auto& pool = frame->m_slots[slot].shadows;
    if (index == pool.size())
    {
        AuxiliaryList candidate;
        const auto create = CreateList(*device, candidate.allocator, candidate.list);
        if (FAILED(create))
            return create;
        try { pool.push_back(std::move(candidate)); }
        catch (const std::exception&) { return E_OUTOFMEMORY; }
    }
    auto& pair = pool[index];
    const HRESULT hr = OpenList(pair);
    if (SUCCEEDED(hr))
    {
        ++frame->m_shadowCount;
        result = pair.list.Get();
    }
    return hr;
}

HRESULT GameDx12NativeFrame12::Recording::CloseShadowList(uint32_t index) const noexcept
{
    if (!UsesShadowLists() || !frame->m_recording || slot != frame->m_slot || index >= frame->m_shadowCount)
        return E_UNEXPECTED;
    return CloseList(frame->m_slots[slot].shadows[index]);
}

HRESULT GameDx12NativeFrame12::Recording::BeginColorList() noexcept
{
    if (!UsesShadowLists() || !frame->m_recording || slot != frame->m_slot || frame->m_shadowCount == 0)
        return E_UNEXPECTED;
    auto& current = frame->m_slots[slot];
    if (frame->m_activeList != current.list.Get())
        return E_UNEXPECTED;
    for (uint32_t index = 0; index < frame->m_shadowCount; ++index)
        if (!current.shadows[index].recorded)
            return E_UNEXPECTED;
    frame->m_activeList = nullptr;
    HRESULT hr = current.list->Close();
    if (SUCCEEDED(hr))
        hr = OpenList(current.colorPass);
    if (SUCCEEDED(hr))
        list = frame->m_activeList = current.colorPass.list.Get();
    return hr;
}

HRESULT GameDx12NativeFrame12::Recording::SetShadowTimingViews(std::span<const uint32_t> ids) const noexcept
{
    if (!frame || !frame->m_recording || frame->m_slot != slot)
        return E_UNEXPECTED;
    auto& current = frame->m_slots[slot];
    if (!current.timingQueries)
        return S_FALSE;
    if (ids.size() > current.timingCapacity || !std::is_sorted(ids.begin(), ids.end()) ||
        std::adjacent_find(ids.begin(), ids.end()) != ids.end())
        return E_INVALIDARG;
    try { current.timingViews.assign(ids.begin(), ids.end()); }
    catch (const std::exception&) { return E_OUTOFMEMORY; }
    return S_OK;
}

void GameDx12NativeFrame12::Recording::StampShadow(ID3D12GraphicsCommandList& target, uint32_t projection, bool end) const noexcept
{
    if (!frame)
        return;
    const auto& current = frame->m_slots[slot];
    const auto& ids = current.timingViews;
    const auto it = std::lower_bound(ids.begin(), ids.end(), projection);
    if (it != ids.end() && *it == projection)
        target.EndQuery(current.timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            4 + 2 * static_cast<uint32_t>(it - ids.begin()) + (end ? 1 : 0));
}

void GameDx12NativeFrame12::Recording::StampSceneStart() const noexcept
{
    if (frame && frame->m_slots[slot].timingQueries)
    {
        frame->m_timingScene = true;
        list->EndQuery(frame->m_slots[slot].timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
    }
}

void GameDx12NativeFrame12::Recording::BeginWater() const noexcept
{
    constexpr auto shaderRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    const D3D12_RESOURCE_BARRIER toCopy[] = {
        ColorBarrier(color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
        ColorBarrier(opaqueColor, shaderRead, D3D12_RESOURCE_STATE_COPY_DEST)
    };
    list->ResourceBarrier(2, toCopy);
    list->CopyResource(opaqueColor, color);
    const D3D12_RESOURCE_BARRIER toWater[] = {
        ColorBarrier(color, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        ColorBarrier(opaqueColor, D3D12_RESOURCE_STATE_COPY_DEST, shaderRead),
        ColorBarrier(depth, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                     D3D12_RESOURCE_STATE_DEPTH_READ | shaderRead)
    };
    list->ResourceBarrier(3, toWater);
    list->OMSetRenderTargets(1, &rtv, FALSE, &readOnlyDsv);
}

void GameDx12NativeFrame12::Recording::EndWater() const noexcept
{
    list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    const auto barrier = ColorBarrier(depth,
        D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    list->ResourceBarrier(1, &barrier);
    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
}

HRESULT GameDx12NativeFrame12::Cancel() noexcept
{
    const auto close = CloseRecording();
    if (auto* descriptors = std::exchange(m_pendingShadowDescriptors, nullptr))
    {
        const auto cancel = descriptors->CancelUnsubmitted();
        if (FAILED(cancel) && SUCCEEDED(close))
            return cancel;
    }
    return close;
}

HRESULT GameDx12NativeFrame12::CloseRecording() noexcept
{
    if (!m_recording)
        return S_FALSE;
    auto& slot = m_slots[m_slot];
    HRESULT hr = S_OK;
    for (uint32_t index = 0; index < m_shadowCount; ++index)
    {
        auto& pair = slot.shadows[index];
        if (pair.open)
        {
            const HRESULT close = CloseList(pair);
            if (FAILED(close))
                hr = close;
        }
    }
    if (m_activeList)
    {
        const HRESULT close = m_activeList == slot.list.Get() ? m_activeList->Close() : CloseList(slot.colorPass);
        if (FAILED(close))
            hr = close;
    }
    m_activeList = nullptr;
    m_shadowCount = 0;
    m_recordingFence = nullptr;
    m_recording = false;
    return hr;
}

bool GameDx12NativeFrame12::RecordCapture(ID3D12Device& device, Slot& slot, ID3D12GraphicsCommandList& list)
{
    if (!m_captureReadback)
    {
        if (m_width > captureMaxWidth || m_height > captureMaxHeight)
        {
            Msg("ph_dx12_capture: viewport exceeds capture limit %ux%u",
                captureMaxWidth, captureMaxHeight);
            return false;
        }
        const auto source = slot.color->GetDesc();
        uint64_t bytes = 0;
        device.GetCopyableFootprints(&source, 0, 1, 0, &m_captureLayout, nullptr, nullptr, &bytes);
        constexpr uint64_t alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
        m_captureStride = (bytes + alignment - 1) & ~(alignment - 1);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = m_captureStride * captureFrames;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr = device.CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(m_captureReadback.GetAddressOf()));
        if (FAILED(hr))
        {
            Msg("ph_dx12_capture: readback allocation failed hr=0x%08X", static_cast<unsigned>(hr));
            return false;
        }
    }

    D3D12_TEXTURE_COPY_LOCATION capture{};
    capture.pResource = m_captureReadback.Get();
    capture.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    capture.PlacedFootprint = m_captureLayout;
    capture.PlacedFootprint.Offset = m_captureStride * m_captureCount;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = slot.color.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const auto toCopy = ColorBarrier(slot.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE);
    list.ResourceBarrier(1, &toCopy);
    list.CopyTextureRegion(&capture, 0, 0, 0, &source, nullptr);
    return true;
}

HRESULT GameDx12NativeFrame12::FinishCapture(const Owner& owner, uint64_t fence)
{
    const HRESULT complete = Wait(owner, fence);
    if (FAILED(complete))
        return complete;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(m_captureStride * captureFrames)};
    void* pixels = nullptr;
    const HRESULT map = m_captureReadback->Map(0, &range, &pixels);
    bool written = false;
    if (SUCCEEDED(map))
    {
        std::FILE* output = nullptr;
        if (_wfopen_s(&output, m_capturePath, L"wb") == 0)
        {
            written = std::fwrite(pixels, 1, range.End, output) == range.End;
            written = std::fclose(output) == 0 && written;
        }
        const D3D12_RANGE noWrite{};
        m_captureReadback->Unmap(0, &noWrite);
    }
    Msg("ph_dx12_capture: %s frames=%u first_present=%llu last_present=%llu fence=%llu "
        "width=%u height=%u pitch=%u stride=%llu",
        written ? "saved" : "write failed", captureFrames,
        static_cast<unsigned long long>(m_presentCount - captureFrames + 1),
        static_cast<unsigned long long>(m_presentCount), static_cast<unsigned long long>(fence),
        m_width, m_height, m_captureLayout.Footprint.RowPitch,
        static_cast<unsigned long long>(m_captureStride));
    m_capturePath[0] = L'\0';
    m_captureReadback.Reset();
    return S_OK;
}

HRESULT GameDx12NativeFrame12::BeginScreenOverlay(ID3D12GraphicsCommandList*& list) noexcept
{
    list = nullptr;
    if (!m_recording || m_unknown || m_submitted || !m_activeList ||
        !m_width || !m_height || m_slot >= kFrameCount)
        return E_UNEXPECTED;
    auto& slot = m_slots[m_slot];
    if (m_shadowCount != 0)
    {
        if (m_activeList != slot.colorPass.list.Get() || !slot.colorPass.open)
            return E_UNEXPECTED;
    }
    else if (m_activeList != slot.list.Get())
        return E_UNEXPECTED;
    // The caller must have restored the swapchain color to RENDER_TARGET
    // (e.g. EndWater). Screen overlays never bind the scene DSV.
    const auto rtv = Rtv(m_slot);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(m_width),
        static_cast<float>(m_height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    m_activeList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_activeList->RSSetViewports(1, &viewport);
    m_activeList->RSSetScissorRects(1, &scissor);
    list = m_activeList;
    return S_OK;
}

HRESULT GameDx12NativeFrame12::Present(const Owner& owner, uint64_t sceneFrame)
{
    if (!m_recording || m_unknown)
        return E_FAIL;
    if (owner.fenceValue >= removedFenceValue - 1)
    {
        Cancel();
        return E_FAIL;
    }
    auto& slot = m_slots[m_slot];
    if (!m_activeList || (m_shadowCount != 0 && m_activeList != slot.colorPass.list.Get()))
    {
        Cancel();
        return E_UNEXPECTED;
    }
    const bool sceneDrawn = sceneFrame != 0;
    if (slot.timingQueries && sceneDrawn)
    {
        constexpr uint32_t query = 0;
        m_activeList->EndQuery(slot.timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
        const auto resolve = [&](uint32_t first, uint32_t count)
        {
            m_activeList->ResolveQueryData(slot.timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                first, count, slot.timingReadback.Get(), first * sizeof(uint64_t));
        };
        if (m_timingScene)
            m_activeList->EndQuery(slot.timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 3);
        resolve(query, m_timingScene ? 4u : 2u);
        if (!slot.timingViews.empty())
            resolve(4, 2 * static_cast<uint32_t>(slot.timingViews.size()));
    }
    bool captured = false;
    if (m_capturePath[0] && sceneFrame >= captureSceneStart)
    {
        captured = RecordCapture(owner.device, slot, *m_activeList);
        if (!captured)
            m_capturePath[0] = L'\0';
    }
    else if (m_captureReadback)
    {
        // The batch must contain consecutive scene presents.
        m_captureCount = 0;
    }
    const auto barrier = ColorBarrier(slot.color.Get(), captured ? D3D12_RESOURCE_STATE_COPY_SOURCE
                                                                  : D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_PRESENT);
    m_activeList->ResourceBarrier(1, &barrier);
    // Один batch гарантирует порядок: начало кадра, карты, цвет и Present.
    auto& lists = m_submitLists;
    lists.clear();
    try
    {
        lists.reserve(static_cast<size_t>(m_shadowCount) + 2);
        lists.push_back(slot.list.Get());
        for (uint32_t index = 0; index < m_shadowCount; ++index)
            lists.push_back(slot.shadows[index].list.Get());
        if (m_shadowCount != 0)
            lists.push_back(slot.colorPass.list.Get());
    }
    catch (const std::exception&)
    {
        Cancel();
        return E_OUTOFMEMORY;
    }
    const uint64_t value = owner.fenceValue + 1;
    if (m_pendingShadowDescriptors && !m_pendingShadowDescriptors->Expects(owner.fence, value))
    {
        Cancel();
        return E_UNEXPECTED;
    }
    const HRESULT close = CloseRecording();
    if (FAILED(close))
    {
        Cancel();
        return close;
    }

    m_submitted = true;
    owner.queue.ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
    if (m_presentCount == 79)
    {
        const char* probe = std::getenv("OGSR_DX12_GAME_NATIVE_REMOVE_DEVICE");
        if (probe && std::strcmp(probe, "1") == 0)
        {
            Ptr<ID3D12Device5> removable;
            if (SUCCEEDED(owner.device.QueryInterface(IID_PPV_ARGS(removable.GetAddressOf()))))
            {
                Msg("ph_dx12_f42: fault device removal after ExecuteCommandLists present=80");
                removable->RemoveDevice();
            }
            else
                Msg("ph_dx12_f42: fault device removal unavailable (ID3D12Device5)");
        }
    }
    const HRESULT signal = owner.queue.Signal(&owner.fence, value);
    auto* descriptors = std::exchange(m_pendingShadowDescriptors, nullptr);
    if (FAILED(signal))
    {
        if (descriptors)
            descriptors->Quarantine();
        m_unknown = true;
        return signal;
    }
    owner.fenceValue = slot.fence = value;
    if (descriptors)
    {
        const auto commit = descriptors->CommitSubmitted(owner.fence, value);
        if (FAILED(commit))
        {
            m_unknown = true;
            return commit;
        }
    }
    const HRESULT hr = m_swapchain->Present(1, 0);
    if (hr == S_OK)
    {
        ++m_presentCount;
        if (slot.timingQueries && sceneDrawn)
        {
            slot.timingPresent = m_presentCount;

            slot.timingScene = m_timingScene;
        }
        if (captured)
        {
            Msg("ph_dx12_capture: sequence=%u present=%llu scene_frame=%llu fence=%llu slot=%u",
                m_captureCount + 1, static_cast<unsigned long long>(m_presentCount),
                static_cast<unsigned long long>(sceneFrame), static_cast<unsigned long long>(value), m_slot);
            if (++m_captureCount == captureFrames)
                return FinishCapture(owner, value);
        }
    }
    else if (captured)
        m_captureCount = 0;
    return hr;
}

void GameDx12NativeFrame12::Release() noexcept
{
    if (m_unknown || m_submitted)
    {
        Quarantine();
        return;
    }
    Cancel();
    for (auto& slot : m_slots)
        slot = {};
    m_depth.Reset();
    m_dsv.Reset();
    m_rtv.Reset();
    m_swapchain.Reset();
    m_captureReadback.Reset();
}

void GameDx12NativeFrame12::Quarantine() noexcept
{
    if (auto* descriptors = std::exchange(m_pendingShadowDescriptors, nullptr))
        descriptors->Quarantine();
    m_recordingFence = nullptr;
    // Fence без подтверждения: ссылки живут до завершения процесса.
    m_unknown = true;
    m_recording = false;
    m_activeList = nullptr;
    m_shadowCount = 0;
    for (auto& slot : m_slots)
    {
        slot.color.Detach();
        slot.opaqueColor.Detach();
        slot.allocator.Detach();
        slot.list.Detach();
        for (auto& pair : slot.shadows)
        {
            pair.allocator.Detach();
            pair.list.Detach();
        }
        slot.colorPass.allocator.Detach();
        slot.colorPass.list.Detach();
        slot.timingQueries.Detach();
        slot.timingReadback.Detach();
    }
    m_depth.Detach();
    m_dsv.Detach();
    m_rtv.Detach();
    m_swapchain.Detach();
    m_captureReadback.Detach();
}

#endif
