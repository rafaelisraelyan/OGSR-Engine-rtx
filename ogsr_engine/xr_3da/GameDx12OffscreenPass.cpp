#include "stdafx.h"
#include "GameDx12OffscreenPass.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12BridgeQuarantine.h"

#include <Windows.h>
#include <wincodec.h>

#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
bool Fail(std::string& reason, const char* text)
{
    reason = text != nullptr ? text : "offscreen pass failure";
    return false;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t size) noexcept
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

class ComInitializationGuard
{
public:
    explicit ComInitializationGuard(HRESULT result) noexcept
        : m_active(SUCCEEDED(result))
    {
    }

    ~ComInitializationGuard()
    {
        if (m_active)
            ::CoUninitialize();
    }

    ComInitializationGuard(const ComInitializationGuard&) = delete;
    ComInitializationGuard& operator=(const ComInitializationGuard&) = delete;

private:
    bool m_active = false;
};
} // namespace

GameDx12OffscreenPass12::~GameDx12OffscreenPass12()
{
    if (m_created)
    {
        static BridgeQuarantineStorage emergency;
        Quarantine(emergency);
    }
}

bool GameDx12OffscreenPass12::Create(ID3D12Device* device, uint32_t width,
                                     uint32_t height, std::string& reason)
{
    if (m_created)
        return Fail(reason, "offscreen pass already created");
    if (device == nullptr || width == 0 || height == 0)
        return Fail(reason, "invalid offscreen pass dimensions/device");

    m_width = width;
    m_height = height;
    m_rowPitch = (width * 4u + 255u) & ~255u;
    m_readbackBytes = static_cast<uint64_t>(m_rowPitch) * height;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = width;
    colorDesc.Height = height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE colorClear = {};
    colorClear.Format = colorDesc.Format;
    colorClear.Color[0] = kGameDx12DiagnosticClearColor[0];
    colorClear.Color[1] = kGameDx12DiagnosticClearColor[1];
    colorClear.Color[2] = kGameDx12DiagnosticClearColor[2];
    colorClear.Color[3] = kGameDx12DiagnosticClearColor[3];

    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &colorDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
        IID_PPV_ARGS(m_color.GetAddressOf()));
    if (FAILED(hr))
    {
        Release();
        return Fail(reason, "offscreen color target creation failed");
    }

    D3D12_RESOURCE_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = depthDesc.Format;
    depthClear.DepthStencil.Depth = 1.0f;
    hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
        IID_PPV_ARGS(m_depth.GetAddressOf()));
    if (FAILED(hr))
    {
        Release();
        return Fail(reason, "offscreen depth target creation failed");
    }

    const D3D12_RESOURCE_DESC readbackDesc = BufferDesc(m_readbackBytes);
    hr = device->CreateCommittedResource(
        &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(m_readback.GetAddressOf()));
    if (FAILED(hr))
    {
        Release();
        return Fail(reason, "offscreen readback buffer creation failed");
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    hr = device->CreateDescriptorHeap(&rtvDesc,
                                      IID_PPV_ARGS(m_rtvHeap.GetAddressOf()));
    if (FAILED(hr))
    {
        Release();
        return Fail(reason, "offscreen RTV heap creation failed");
    }
    m_rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(m_color.Get(), nullptr, m_rtv);

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    hr = device->CreateDescriptorHeap(&dsvDesc,
                                      IID_PPV_ARGS(m_dsvHeap.GetAddressOf()));
    if (FAILED(hr))
    {
        Release();
        return Fail(reason, "offscreen DSV heap creation failed");
    }
    m_dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateDepthStencilView(m_depth.Get(), nullptr, m_dsv);

    try
    {
        m_pixels.assign(static_cast<size_t>(m_readbackBytes), 0);
    }
    catch (const std::bad_alloc&)
    {
        Release();
        return Fail(reason, "offscreen pixel allocation failed");
    }
    catch (const std::length_error&)
    {
        Release();
        return Fail(reason, "offscreen pixel allocation failed");
    }

    m_created = true;
    m_colorCopySource = false;
    return true;
}

bool GameDx12OffscreenPass12::RecordBegin(
    ID3D12GraphicsCommandList* list, const float clearColor[4],
    std::string& reason)
{
    if (!m_created || list == nullptr || clearColor == nullptr)
        return Fail(reason, "offscreen pass is not ready");
    m_colorCopySourceBeforeRecord = m_colorCopySource;
    m_recordStarted = true;
    if (m_colorCopySource)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_color.Get();
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &barrier);
        m_colorCopySource = false;
    }

    list->OMSetRenderTargets(1, &m_rtv, FALSE, &m_dsv);
    list->ClearRenderTargetView(m_rtv, clearColor, 0, nullptr);
    list->ClearDepthStencilView(m_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                                nullptr);
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width),
                          static_cast<LONG>(m_height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    return true;
}

bool GameDx12OffscreenPass12::RecordEnd(ID3D12GraphicsCommandList* list,
                                        std::string& reason)
{
    if (!m_created || list == nullptr || m_colorCopySource)
        return Fail(reason, "offscreen pass is not in render state");

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_color.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    m_colorCopySource = true;

    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = m_color.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = m_readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Offset = 0;
    destination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    destination.PlacedFootprint.Footprint.Width = m_width;
    destination.PlacedFootprint.Footprint.Height = m_height;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = m_rowPitch;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    return true;
}

void GameDx12OffscreenPass12::AbortUnsubmittedFrame() noexcept
{
    if (!m_recordStarted)
        return;
    m_colorCopySource = m_colorCopySourceBeforeRecord;
    m_recordStarted = false;
}

void GameDx12OffscreenPass12::CommitSubmittedFrame() noexcept
{
    m_recordStarted = false;
}

bool GameDx12OffscreenPass12::Readback(std::string& reason)
{
    if (!m_created || m_readback == nullptr || m_pixels.empty())
        return Fail(reason, "offscreen readback is not ready");
    if (!ValidateGameDx12ReadbackLayout(
            m_width, m_height, m_rowPitch, m_readbackBytes,
            static_cast<uint64_t>(m_pixels.size())))
        return Fail(reason, "offscreen readback buffer size mismatch");
    void* mapped = nullptr;
    D3D12_RANGE range = {0, static_cast<SIZE_T>(m_readbackBytes)};
    const HRESULT hr = m_readback->Map(0, &range, &mapped);
    if (FAILED(hr) || mapped == nullptr)
        return Fail(reason, "offscreen readback Map failed");
    std::memcpy(m_pixels.data(), mapped,
                static_cast<size_t>(m_readbackBytes));
    m_readback->Unmap(0, nullptr);
    return true;
}

GameDx12ReadbackCoverage12 GameDx12OffscreenPass12::EvaluateCoverage()
    const noexcept
{
    if (!ValidateGameDx12ReadbackLayout(
            m_width, m_height, m_rowPitch, m_readbackBytes,
            static_cast<uint64_t>(m_pixels.size())))
        return {};
    return EvaluateGameDx12ReadbackCoverage(
        m_pixels.data(), m_width, m_height, m_rowPitch);
}

bool GameDx12OffscreenPass12::SavePng(const wchar_t* path,
                                      std::string& reason) const
{
    if (!m_created || path == nullptr || m_pixels.empty())
        return Fail(reason, "offscreen PNG is not ready");
    if (!ValidateGameDx12ReadbackLayout(
            m_width, m_height, m_rowPitch, m_readbackBytes,
            static_cast<uint64_t>(m_pixels.size())))
        return Fail(reason, "offscreen PNG readback buffer size mismatch");

    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co) && co != RPC_E_CHANGED_MODE)
    {
        return Fail(reason, "offscreen PNG CoInitializeEx failed");
    }
    // Declared before every WIC ComPtr so WIC objects are destroyed before
    // this guard balances S_OK/S_FALSE. RPC_E_CHANGED_MODE stays unbalanced.
    ComInitializationGuard comGuard(co);

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG WIC factory failed");

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG stream failed");
    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG path failed");

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                encoder.GetAddressOf());
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG encoder failed");
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG encoder init failed");

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(),
                                 properties.GetAddressOf());
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG frame failed");
    hr = frame->Initialize(properties.Get());
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG frame init failed");
    hr = frame->SetSize(m_width, m_height);
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG size failed");

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG format failed");

    std::vector<uint8_t> tight;
    try
    {
        tight.resize(static_cast<size_t>(m_width) * m_height * 4u);
    }
    catch (const std::bad_alloc&)
    {
        return Fail(reason, "offscreen PNG pixel allocation failed");
    }
    const bool bgra = IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA);
    const bool rgba = IsEqualGUID(format, GUID_WICPixelFormat32bppRGBA);
    if (!bgra && !rgba)
        return Fail(reason, "offscreen PNG format negotiation failed");

    for (uint32_t y = 0; y < m_height; ++y)
    {
        const uint8_t* source =
            m_pixels.data() + static_cast<size_t>(y) * m_rowPitch;
        uint8_t* destination =
            tight.data() + static_cast<size_t>(y) * m_width * 4u;
        for (uint32_t x = 0; x < m_width; ++x)
        {
            destination[x * 4 + 0] = bgra ? source[x * 4 + 2] : source[x * 4 + 0];
            destination[x * 4 + 1] = source[x * 4 + 1];
            destination[x * 4 + 2] = bgra ? source[x * 4 + 0] : source[x * 4 + 2];
            destination[x * 4 + 3] = source[x * 4 + 3];
        }
    }

    hr = frame->WritePixels(m_height, m_width * 4u,
                            static_cast<UINT>(tight.size()),
                            tight.data());
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG write failed");
    hr = frame->Commit();
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG frame commit failed");
    hr = encoder->Commit();
    if (FAILED(hr))
        return Fail(reason, "offscreen PNG encoder commit failed");
    return true;
}

void GameDx12OffscreenPass12::Release()
{
    m_color.Reset();
    m_depth.Reset();
    m_readback.Reset();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_rtv = {};
    m_dsv = {};
    m_width = 0;
    m_height = 0;
    m_rowPitch = 0;
    m_readbackBytes = 0;
    m_created = false;
    m_colorCopySource = false;
    m_recordStarted = false;
    m_colorCopySourceBeforeRecord = false;
    m_pixels.clear();
}

void GameDx12OffscreenPass12::Quarantine(
    BridgeQuarantineStorage& quarantine)
{
    if (m_color != nullptr)
        quarantine.offscreenColor = m_color.Detach();
    if (m_depth != nullptr)
        quarantine.offscreenDepth = m_depth.Detach();
    if (m_readback != nullptr)
        quarantine.offscreenReadback = m_readback.Detach();
    if (m_rtvHeap != nullptr)
        quarantine.offscreenRtvHeap = m_rtvHeap.Detach();
    if (m_dsvHeap != nullptr)
        quarantine.offscreenDsvHeap = m_dsvHeap.Detach();
    m_rtv = {};
    m_dsv = {};
    m_width = 0;
    m_height = 0;
    m_rowPitch = 0;
    m_readbackBytes = 0;
    m_created = false;
    m_colorCopySource = false;
    m_recordStarted = false;
    m_colorCopySourceBeforeRecord = false;
    m_pixels.clear();
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
