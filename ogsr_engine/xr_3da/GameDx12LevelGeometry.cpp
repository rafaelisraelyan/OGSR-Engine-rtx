#include "stdafx.h"
#include "GameDx12LevelGeometry.h"
#include "dx12xraylevelasset.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "FS.h"
#include "LocatorAPI_defs.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
GpuQuarantine g_geometryQuarantine;
ID3D12Resource* g_geometryConstantQuarantine = nullptr;

bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix) noexcept
{
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            if (!std::isfinite(matrix.m[row][column]))
                return false;
        }
    }
    return true;
}

DirectX::XMFLOAT4X4 Transpose(const DirectX::XMFLOAT4X4& matrix) noexcept
{
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(
        &result, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&matrix)));
    return result;
}

D3D12_RESOURCE_DESC UploadBufferDesc(uint64_t size) noexcept
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

uint64_t NextGeneration(uint64_t session, uint64_t epoch,
                        uint64_t previous) noexcept
{
    uint64_t value = (session * 0x9E3779B97F4A7C15ull) ^
                     (epoch * 0xBF58476D1CE4E5B9ull);
    if (value == 0 || value == previous)
        ++value;
    if (value == 0)
        value = 1;
    return value;
}
} // namespace

GameDx12LevelGeometry12::~GameDx12LevelGeometry12()
{
    // If a draw was recorded but completion was not confirmed, the registry is
    // moved to process-lifetime quarantine instead of calling Release().
    Shutdown(m_uploadConfirmed && !m_drawRecorded && !m_gpuUnknown);
}

bool GameDx12LevelGeometry12::Fail(std::string& reason, const char* text)
{
    m_lastError = text != nullptr ? text : "level geometry failure";
    reason = m_lastError;
    return false;
}

bool GameDx12LevelGeometry12::Fail(std::string& reason,
                                   const std::string& text)
{
    m_lastError = text;
    reason = m_lastError;
    return false;
}

void GameDx12LevelGeometry12::InvalidateFrame()
{
    m_prepared = false;
    m_resolved = {};
}

bool GameDx12LevelGeometry12::ReadVfsFile(const char* name,
                                          uint64_t maxBytes,
                                          std::vector<uint8_t>& bytes,
                                          std::string& reason)
{
    IReader* reader = FS.r_open(fsgame::level, name);
    if (reader == nullptr)
        return Fail(reason, std::string("VFS open failed: ") + name);

    const size_t size = reader->length();
    if (size == 0 || static_cast<uint64_t>(size) > maxBytes)
    {
        FS.r_close(reader);
        return Fail(reason, std::string("VFS size rejected: ") + name);
    }

    try
    {
        bytes.resize(size);
    }
    catch (const std::bad_alloc&)
    {
        FS.r_close(reader);
        return Fail(reason, "VFS read allocation failed");
    }
    catch (const std::length_error&)
    {
        FS.r_close(reader);
        return Fail(reason, "VFS read allocation failed");
    }

    std::memcpy(bytes.data(), reader->begin(), size);
    FS.r_close(reader);
    return true;
}

bool GameDx12LevelGeometry12::CreateConstants(ID3D12Device* device,
                                              std::string& reason)
{
    if (device == nullptr)
        return Fail(reason, "invalid device for constants");

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const D3D12_RESOURCE_DESC desc = UploadBufferDesc(512);
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(m_constants.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "constant buffer creation failed");

    D3D12_RANGE noRead = {0, 0};
    hr = m_constants->Map(0, &noRead, &m_constantsMapped);
    if (FAILED(hr) || m_constantsMapped == nullptr)
    {
        m_constantsMapped = nullptr;
        m_constants.Reset();
        return Fail(reason, "constant buffer map failed");
    }
    std::memset(m_constantsMapped, 0, 512);
    m_frameCBVA = m_constants->GetGPUVirtualAddress();
    m_objectCBVA = m_frameCBVA + 256;
    return true;
}

bool GameDx12LevelGeometry12::LoadFromVfs(
    const GameDx12LevelGeometryOwner12& owner,
    const char* levelName,
    uint32_t visualId,
    uint64_t levelSession,
    uint64_t levelEpoch,
    std::string& reason)
{
    try
    {
        return LoadFromVfsImpl(owner, levelName, visualId, levelSession,
                               levelEpoch, reason);
    }
    catch (const std::bad_alloc&)
    {
        if (m_uploadSubmitted && !m_uploadConfirmed)
            QuarantineGpu();
        else if (m_uploadConfirmed && !m_drawRecorded)
            ReleaseGpu();
        InvalidateFrame();
        m_loaded = false;
        reason.clear();
        return false;
    }
    catch (const std::length_error&)
    {
        if (m_uploadSubmitted && !m_uploadConfirmed)
            QuarantineGpu();
        else if (m_uploadConfirmed && !m_drawRecorded)
            ReleaseGpu();
        InvalidateFrame();
        m_loaded = false;
        reason.clear();
        return false;
    }
}

bool GameDx12LevelGeometry12::LoadFromVfsImpl(
    const GameDx12LevelGeometryOwner12& owner,
    const char* levelName,
    uint32_t visualId,
    uint64_t levelSession,
    uint64_t levelEpoch,
    std::string& reason)
{
    m_lastError.clear();
    if (owner.device == nullptr || owner.queue == nullptr ||
        owner.fence == nullptr || owner.fenceEvent == nullptr ||
        owner.allocator == nullptr || owner.list == nullptr ||
        owner.fenceValue == nullptr)
        return Fail(reason, "invalid GameDx12Bridge owner context");
    if (levelName == nullptr || levelName[0] == '\0')
        return Fail(reason, "missing level name");
    if (levelSession == 0 || levelEpoch == 0)
        return Fail(reason, "invalid level geometry identity");
    if (m_gpuUnknown)
        return Fail(reason, "level geometry is quarantined");
    if (m_loaded || m_registry.IsCreated() || m_uploadSubmitted)
        return Fail(reason, "level geometry is already loaded");
    if (*owner.fenceValue == std::numeric_limits<uint64_t>::max())
        return Fail(reason, "bridge fence value overflow");

    xraylevel12::Limits limits{};
    std::vector<uint8_t> levelBytes;
    std::vector<uint8_t> geomBytes;
    if (!ReadVfsFile(fsgame::level_files::level, limits.maxLevelBytes,
                     levelBytes, reason) ||
        !ReadVfsFile(fsgame::level_files::level_geom, limits.maxGeomBytes,
                     geomBytes, reason))
        return false;

    xraylevel12::Geometry geometry;
    std::string readReason;
    const xraylevel12::ReadError readError = xraylevel12::ReadVisual(
        levelBytes, geomBytes, visualId, geometry, readReason, limits);
    if (readError != xraylevel12::ReadError::None)
    {
        std::string message = "ReadVisual failed: ";
        message += xraylevel12::ReadErrorMessage(readError);
        if (!readReason.empty())
        {
            message += " (";
            message += readReason;
            message += ")";
        }
        return Fail(reason, message);
    }
    if (geometry.visual_id != visualId)
        return Fail(reason, "ReadVisual returned a different visual id");

    XrayAssetData12 asset;
    const char* assetReason = nullptr;
    if (!BuildXrayLevelAsset12(geometry, asset, &assetReason))
        return Fail(reason, assetReason != nullptr ? assetReason
                                                   : "asset build failed");

    // Mark the submission window before the call: if CreateXrayAsset throws
    // after ExecuteCommandLists, the outer catch must quarantine rather than
    // release potentially in-flight GPU resources. A false return happens
    // before submission on every current failure path.
    m_uploadSubmitted = true;
    const bool created = m_registry.CreateXrayAsset(
        owner.device, owner.queue, owner.allocator, owner.list, asset, nullptr);
    if (!created)
    {
        m_uploadSubmitted = false;
        m_registry.Release();
        return Fail(reason, "CreateXrayAsset failed");
    }

    const uint64_t expected = *owner.fenceValue + 1;
    HRESULT hr = owner.queue->Signal(owner.fence, expected);
    if (FAILED(hr))
    {
        QuarantineGpu();
        return Fail(reason, "upload fence signal failed");
    }
    // Advance the owner counter even if the wait later fails: the queue has
    // already been given this value, and a later service submit must not reuse
    // it.
    *owner.fenceValue = expected;
    if (owner.fence->GetCompletedValue() < expected)
    {
        hr = owner.fence->SetEventOnCompletion(expected, owner.fenceEvent);
        if (FAILED(hr))
        {
            QuarantineGpu();
            return Fail(reason, "upload fence event failed");
        }
        const DWORD wait = WaitForSingleObject(owner.fenceEvent,
                                               kUploadFenceTimeoutMs);
        if (wait != WAIT_OBJECT_0)
        {
            QuarantineGpu();
            return Fail(reason, "upload fence wait failed");
        }
    }
    if (owner.fence->GetCompletedValue() < expected ||
        owner.device->GetDeviceRemovedReason() != S_OK)
    {
        QuarantineGpu();
        return Fail(reason, "upload fence completion unconfirmed");
    }

    m_uploadFenceValue = expected;
    m_uploadConfirmed = true;
    m_registry.OnUploadComplete();

    if (!CreateConstants(owner.device, reason))
    {
        // Upload is confirmed and no draw has been recorded, so releasing the
        // registry here cannot race with GPU work.
        ReleaseGpu();
        return false;
    }

    m_asset = std::move(asset);
    m_visualId = visualId;
    m_assetIndexCount = m_registry.AssetIndexCount();
    m_levelSession = levelSession;
    m_levelEpoch = levelEpoch;
    m_generation = NextGeneration(levelSession, levelEpoch, 0);
    m_loaded = true;
    Msg("ph_dx12_geometry: loaded level='%s' visual=%u session=%llu "
        "epoch=%llu verts=%zu indices=%u generation=%llu",
        levelName, visualId,
        static_cast<unsigned long long>(m_levelSession),
        static_cast<unsigned long long>(m_levelEpoch),
        m_asset.vertices.size(), m_assetIndexCount,
        static_cast<unsigned long long>(m_generation));
    return true;
}

bool GameDx12LevelGeometry12::PrepareFrame(
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const DirectX::XMFLOAT4X4& logicalWorld,
    std::string& reason)
{
    m_lastError.clear();
    if (!m_loaded || !m_uploadConfirmed || m_gpuUnknown ||
        m_generation == 0)
        return Fail(reason, "level geometry is not uploaded");
    if (m_drawRecorded)
        return Fail(reason, "previous draw fence is not confirmed");
    if (!FiniteMatrix(logicalViewProjection) || !FiniteMatrix(logicalWorld))
        return Fail(reason, "non-finite camera or world matrix");

    dx12object12::ObjectConstants12 objectConstants{};
    if (!dx12object12::BuildObjectConstants12(
            logicalWorld, {1.0f, 1.0f, 1.0f, 1.0f}, objectConstants))
        return Fail(reason, "invalid world transform");
    if (m_constantsMapped == nullptr)
        return Fail(reason, "constant buffer is not mapped");

    InvalidateFrame();
    try
    {
        renderprep12::PreparedFrame12 prepared;
        prepared.generation = m_generation;
        prepared.shaderCamera = Transpose(logicalViewProjection);

        renderprep12::DrawPacket12 packet;
        packet.object.id = 1;
        packet.object.indexCount = m_assetIndexCount;
        packet.object.world = logicalWorld;
        packet.object.minimum = m_asset.minimum;
        packet.object.maximum = m_asset.maximum;
        packet.object.sphere = m_asset.sphere;
        packet.object.visual = kXrayVisual12;
        packet.object.material = kXrayMaterial12;
        packet.object.pso = kXrayPso12;
        packet.object.flags = 0;
        packet.object.opacity = 1.0f;
        packet.worldMinimum = m_asset.minimum;
        packet.worldMaximum = m_asset.maximum;
        packet.worldSphere = m_asset.sphere;
        packet.visible = true;
        prepared.packets.push_back(packet);

        renderprep12::OrderedFrame12 ordered;
        ordered.frame = prepared;
        ordered.categories[0] = {0, 1};
        ordered.categories[1] = {1, 0};
        ordered.categories[2] = {1, 0};

        const renderprep12::FrameBatchPlan12 plan =
            renderprep12::BuildPacketBatchPlan(ordered, 1, 1);
        const renderprep12::BatchPlanValidation12 validation =
            renderprep12::ValidatePacketBatchPlan(ordered, plan);
        if (!validation.ok || plan.packetCount != 1)
            return Fail(reason, "level geometry packet plan failed");

        ResolvedFrame12 resolved;
        const char* resolveReason = nullptr;
        if (!ResolveXrayFrame(ordered, m_generation,
                              m_registry.AssetVertexView(),
                              m_registry.AssetIndexView(),
                              m_registry.AssetIndexCount(), resolved,
                              &resolveReason) ||
            resolved.draws.size() != 1)
            return Fail(reason, resolveReason != nullptr
                                    ? resolveReason
                                    : "level geometry resolve failed");

        std::memset(m_constantsMapped, 0, 512);
        const DirectX::XMFLOAT4X4 shaderCamera =
            Transpose(logicalViewProjection);
        std::memcpy(m_constantsMapped, &shaderCamera,
                    sizeof(shaderCamera));
        std::memcpy(static_cast<uint8_t*>(m_constantsMapped) + 256,
                    &objectConstants, sizeof(objectConstants));

        m_resolved = std::move(resolved);
        m_prepared = true;
        return true;
    }
    catch (const std::bad_alloc&)
    {
        InvalidateFrame();
        return Fail(reason, "level geometry frame allocation failed");
    }
    catch (const std::length_error&)
    {
        InvalidateFrame();
        return Fail(reason, "level geometry frame allocation failed");
    }
    catch (const std::exception&)
    {
        InvalidateFrame();
        return Fail(reason, "level geometry frame preparation failed");
    }
}

bool GameDx12LevelGeometry12::RecordDraw(ID3D12GraphicsCommandList* list,
                                         std::string& reason)
{
    m_lastError.clear();
    if (list == nullptr)
        return Fail(reason, "null command list");
    if (!m_prepared || m_resolved.draws.size() != 1)
        return Fail(reason, "no prepared level geometry draw");
    if (m_drawRecorded)
        return Fail(reason, "level geometry draw already recorded");
    if (!m_registry.RecordDraw(list, m_resolved.draws[0], m_frameCBVA,
                               m_objectCBVA))
        return Fail(reason, "level geometry RecordDraw failed");
    m_drawFence.Reset();
    m_drawDevice.Reset();
    m_drawRecorded = true;
    m_drawSubmitted = false;
    m_drawCompleted = false;
    m_drawFenceValue = 0;
    return true;
}

bool GameDx12LevelGeometry12::MarkDrawSubmitted(
    uint64_t fenceValue, ID3D12Fence* fence, ID3D12Device* device,
    std::string& reason)
{
    m_lastError.clear();
    if (!m_drawRecorded || m_drawSubmitted || m_drawCompleted)
        return Fail(reason, "no recorded draw awaiting submission");
    if (fence == nullptr || device == nullptr)
        return Fail(reason, "missing owner fence or device");
    if (m_drawFence != nullptr || m_drawDevice != nullptr)
        return Fail(reason, "draw already owns submission objects");
    if (!m_uploadConfirmed || fenceValue == 0 ||
        fenceValue <= m_uploadFenceValue)
        return Fail(reason, "invalid Draw fence value");

    m_drawFence = fence;
    m_drawDevice = device;
    m_drawSubmitted = true;
    m_drawFenceValue = fenceValue;
    return true;
}

bool GameDx12LevelGeometry12::OnDrawCompleted(
    uint64_t fenceValue, ID3D12Fence* fence, ID3D12Device* device,
    std::string& reason)
{
    m_lastError.clear();
    if (!m_drawRecorded || !m_drawSubmitted || m_drawCompleted)
        return Fail(reason, "no submitted draw awaiting completion");
    if (fence == nullptr || device == nullptr)
        return Fail(reason, "missing owner fence or device");
    if (fence != m_drawFence.Get() || device != m_drawDevice.Get())
        return Fail(reason, "Draw fence/device identity does not match submission");
    if (fenceValue == 0 || fenceValue != m_drawFenceValue ||
        fenceValue <= m_uploadFenceValue)
        return Fail(reason, "Draw fence value does not match submission");
    if (fence->GetCompletedValue() < fenceValue)
        return Fail(reason, "Draw fence is not complete");
    if (device->GetDeviceRemovedReason() != S_OK)
        return Fail(reason, "device removed before Draw completion");

    m_drawFence.Reset();
    m_drawDevice.Reset();
    m_drawRecorded = false;
    m_drawSubmitted = false;
    m_drawCompleted = true;
    m_drawFenceValue = 0;
    return true;
}

bool GameDx12LevelGeometry12::AbortRecordedDraw(std::string& reason)
{
    m_lastError.clear();
    if (!m_drawRecorded || m_drawCompleted)
        return Fail(reason, "no recorded draw to abort");
    if (m_drawSubmitted)
        return Fail(reason, "draw already submitted; abort is unsafe");
    m_drawFence.Reset();
    m_drawDevice.Reset();
    m_drawRecorded = false;
    m_drawSubmitted = false;
    m_drawCompleted = false;
    m_drawFenceValue = 0;
    return true;
}

void GameDx12LevelGeometry12::OnLevelTeardown(uint64_t nextEpoch)
{
    m_levelEpoch = nextEpoch;
    InvalidateFrame();
    m_loaded = false;
    if (m_drawRecorded)
    {
        // A recorded or submitted draw has no verified completion on this
        // teardown path. Keep the registry/constants held; Shutdown() must
        // quarantine them.
        m_generation = 0;
        return;
    }
    if (m_uploadSubmitted && !m_uploadConfirmed)
    {
        QuarantineGpu();
        return;
    }
    if (m_uploadConfirmed)
    {
        ReleaseGpu();
        return;
    }
    m_asset = {};
    m_visualId = 0;
    m_assetIndexCount = 0;
    m_levelSession = 0;
    m_generation = 0;
}

void GameDx12LevelGeometry12::OnDeviceReset(uint64_t nextEpoch)
{
    m_levelEpoch = nextEpoch;
    InvalidateFrame();
    if (m_drawRecorded)
    {
        // The old draw has no verified completion. Make the geometry
        // unavailable and let the bridge observe GpuCompletionUnknown().
        m_generation = 0;
        m_loaded = false;
        return;
    }
    if (m_loaded && m_uploadConfirmed)
        m_generation = NextGeneration(m_levelSession, nextEpoch,
                                      m_generation);
    else
        m_generation = 0;
}

void GameDx12LevelGeometry12::ReleaseGpu()
{
    if (m_constantsMapped != nullptr && m_constants != nullptr)
        m_constants->Unmap(0, nullptr);
    m_constantsMapped = nullptr;
    m_constants.Reset();
    m_registry.Release();

    m_asset = {};
    m_resolved = {};
    m_prepared = false;
    m_drawFence.Reset();
    m_drawDevice.Reset();
    m_drawRecorded = false;
    m_drawSubmitted = false;
    m_drawCompleted = false;
    m_drawFenceValue = 0;
    m_uploadSubmitted = false;
    m_uploadConfirmed = false;
    m_loaded = false;
    m_visualId = 0;
    m_assetIndexCount = 0;
    m_levelSession = 0;
    m_levelEpoch = 0;
    m_generation = 0;
    m_uploadFenceValue = 0;
    m_frameCBVA = 0;
    m_objectCBVA = 0;
}

void GameDx12LevelGeometry12::QuarantineGpu()
{
    if (m_constantsMapped != nullptr && m_constants != nullptr)
        m_constants->Unmap(0, nullptr);
    m_constantsMapped = nullptr;
    if (m_constants != nullptr)
        g_geometryConstantQuarantine = m_constants.Detach();

    m_registry.Quarantine(g_geometryQuarantine);
    g_geometryQuarantine.occupied = true;

    // Captured fence/device ComPtrs stay alive intentionally. If completion is
    // unknown they must not be released here; the bridge quarantine retains the
    // owner references until process exit.
    m_asset = {};
    m_resolved = {};
    m_prepared = false;
    m_drawRecorded = false;
    m_drawSubmitted = false;
    m_drawCompleted = false;
    m_drawFenceValue = 0;
    m_uploadSubmitted = false;
    m_uploadConfirmed = false;
    m_loaded = false;
    m_gpuUnknown = true;
    m_visualId = 0;
    m_assetIndexCount = 0;
    m_levelSession = 0;
    m_levelEpoch = 0;
    m_generation = 0;
    m_uploadFenceValue = 0;
    m_frameCBVA = 0;
    m_objectCBVA = 0;
}

void GameDx12LevelGeometry12::Shutdown(bool gpuCompletionConfirmed)
{
    if (m_gpuUnknown)
        return;

    if (gpuCompletionConfirmed && m_uploadConfirmed && !m_drawRecorded)
    {
        ReleaseGpu();
        return;
    }

    if (m_uploadSubmitted || m_uploadConfirmed || m_constants != nullptr)
    {
        QuarantineGpu();
        return;
    }

    InvalidateFrame();
    m_loaded = false;
    m_asset = {};
    m_visualId = 0;
    m_assetIndexCount = 0;
    m_levelSession = 0;
    m_levelEpoch = 0;
    m_generation = 0;
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
