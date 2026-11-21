#include "stdafx.h"
#include "GameDx12NativeFrame.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
constexpr DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr DWORD fenceTimeoutMs = 10000;
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
    descriptors.NumDescriptors = 1;
    hr = owner.device.CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(m_dsv.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    for (auto& slot : m_slots)
    {
        hr = owner.device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(slot.allocator.GetAddressOf()));
        if (FAILED(hr))
            return hr;
        hr = owner.device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             slot.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(slot.list.GetAddressOf()));
        if (FAILED(hr))
            return hr;
        hr = slot.list->Close();
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
    if (measure != nullptr && std::strcmp(measure, "1") == 0 &&
        SUCCEEDED(owner.queue.GetTimestampFrequency(&m_timestampFrequency)) &&
        m_timestampFrequency != 0)
    {
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query.Count = 2 * kFrameCount;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC readback{};
        readback.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readback.Width = query.Count * sizeof(uint64_t);
        readback.Height = 1;
        readback.DepthOrArraySize = 1;
        readback.MipLevels = 1;
        readback.SampleDesc.Count = 1;
        readback.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(owner.device.CreateQueryHeap(&query, IID_PPV_ARGS(m_timingQueries.GetAddressOf()))) ||
            FAILED(owner.device.CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &readback, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(m_timingReadback.GetAddressOf()))))
        {
            m_timingQueries.Reset();
            m_timingReadback.Reset();
            Msg("ph_dx12_native: GPU timestamps unavailable");
        }
    }
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
    depth.Format = depthFormat;
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
    device.CreateDepthStencilView(m_depth.Get(), nullptr, m_dsv->GetCPUDescriptorHandleForHeapStart());
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
    if (SUCCEEDED(hr) && owner.fence.GetCompletedValue() < value)
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
    if (owner.fenceValue == (std::numeric_limits<uint64_t>::max)())
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
        slot.color.Reset();
    hr = m_swapchain->ResizeBuffers(kFrameCount, width, height, colorFormat, 0);
    if (FAILED(hr))
        return hr;
    return CreateTargets(owner.device, width, height);
}

HRESULT GameDx12NativeFrame12::Begin(HWND window, const Owner& owner, Recording& recording)
{
    recording = {};
    m_lastGpuTiming = {};
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
    HRESULT hr = Wait(owner, slot.fence);
    if (FAILED(hr))
        return hr;
    if (m_timingQueries && slot.timingPresent != 0)
    {
        const SIZE_T offset = m_slot * 2 * sizeof(uint64_t);
        const D3D12_RANGE range{offset, offset + 2 * sizeof(uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(m_timingReadback->Map(0, &range, &mapped)))
        {
            uint64_t ticks[2];
            std::memcpy(ticks, static_cast<const uint8_t*>(mapped) + offset, sizeof(ticks));
            if (ticks[1] >= ticks[0])
            {
                m_lastGpuTiming.present = slot.timingPresent;
                m_lastGpuTiming.milliseconds = 1000.0 * static_cast<double>(ticks[1] - ticks[0]) /
                                               static_cast<double>(m_timestampFrequency);
            }
            const D3D12_RANGE noWrite{};
            m_timingReadback->Unmap(0, &noWrite);
        }
        slot.timingPresent = 0;
    }
    hr = slot.allocator->Reset();
    if (FAILED(hr))
        return hr;
    hr = slot.list->Reset(slot.allocator.Get(), nullptr);
    if (FAILED(hr))
        return hr;
    m_recording = true;
    if (m_timingQueries)
        slot.list->EndQuery(m_timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * m_slot);

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
    recording = {slot.list.Get(), m_slot, width, height};
    return S_OK;
}

HRESULT GameDx12NativeFrame12::Cancel() noexcept
{
    if (!m_recording)
        return S_FALSE;
    m_recording = false;
    return m_slots[m_slot].list->Close();
}

bool GameDx12NativeFrame12::RecordCapture(ID3D12Device& device, Slot& slot)
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
    slot.list->ResourceBarrier(1, &toCopy);
    slot.list->CopyTextureRegion(&capture, 0, 0, 0, &source, nullptr);
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

HRESULT GameDx12NativeFrame12::Present(const Owner& owner, uint64_t sceneFrame)
{
    if (!m_recording || m_unknown)
        return E_FAIL;
    if (owner.fenceValue == (std::numeric_limits<uint64_t>::max)())
    {
        Cancel();
        return E_FAIL;
    }
    auto& slot = m_slots[m_slot];
    const bool sceneDrawn = sceneFrame != 0;
    if (m_timingQueries && sceneDrawn)
    {
        const uint32_t query = 2 * m_slot;
        slot.list->EndQuery(m_timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
        slot.list->ResolveQueryData(m_timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                    query, 2, m_timingReadback.Get(), query * sizeof(uint64_t));
    }
    bool captured = false;
    if (m_capturePath[0] && sceneFrame >= captureSceneStart)
    {
        captured = RecordCapture(owner.device, slot);
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
    slot.list->ResourceBarrier(1, &barrier);
    const HRESULT close = Cancel();
    if (FAILED(close))
        return close;

    m_submitted = true;
    ID3D12CommandList* lists[]{slot.list.Get()};
    owner.queue.ExecuteCommandLists(1, lists);
    const uint64_t value = owner.fenceValue + 1;
    const HRESULT signal = owner.queue.Signal(&owner.fence, value);
    if (FAILED(signal))
    {
        m_unknown = true;
        return signal;
    }
    owner.fenceValue = slot.fence = value;
    const HRESULT hr = m_swapchain->Present(1, 0);
    if (hr == S_OK)
    {
        ++m_presentCount;
        if (m_timingQueries && sceneDrawn)
            slot.timingPresent = m_presentCount;
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
    m_timingQueries.Reset();
    m_timingReadback.Reset();
    m_captureReadback.Reset();
}

void GameDx12NativeFrame12::Quarantine() noexcept
{
    // Fence без подтверждения: ссылки живут до завершения процесса.
    m_unknown = true;
    m_recording = false;
    for (auto& slot : m_slots)
    {
        slot.color.Detach();
        slot.allocator.Detach();
        slot.list.Detach();
    }
    m_depth.Detach();
    m_dsv.Detach();
    m_rtv.Detach();
    m_swapchain.Detach();
    m_timingQueries.Detach();
    m_timingReadback.Detach();
    m_captureReadback.Detach();
}

#endif
