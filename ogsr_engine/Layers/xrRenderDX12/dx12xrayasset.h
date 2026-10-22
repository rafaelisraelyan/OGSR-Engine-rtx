#pragma once

#include "dx12geometrycatalog.h"
#include "dx12renderprep.h"

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

// F3.6A: minimal read-only bridge for one validated X-Ray static visual.
// The loader is CPU-only; GPU resources remain owned by GpuMeshRegistry12.
struct XrayVertex12
{
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 normal{};
    DirectX::XMFLOAT2 uv{};
};
static_assert(sizeof(XrayVertex12) == 32,
              "X-Ray FVF 0x112 vertex stride must remain 32 bytes");

struct XrayTextureMip12
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowPitch = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
};

struct XrayTextureData12
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<uint8_t> data;
    std::vector<XrayTextureMip12> mips;
};

struct XrayAssetData12
{
    std::vector<XrayVertex12> vertices;
    std::vector<uint16_t> indices;
    DirectX::XMFLOAT3 minimum{};
    DirectX::XMFLOAT3 maximum{};
    renderprep12::Sphere12 sphere{};
    std::string textureName;
    std::string shaderName;
};

// Separate render-owned identities. These are deliberately not members of
// SyntheticResources12; CPU preparation receives them through an explicit
// allow-list.
inline constexpr renderprep12::Resource12 kXrayVisual12{0x5841u, 1};
inline constexpr renderprep12::Resource12 kXrayMaterial12{0x5842u, 1};
inline constexpr renderprep12::Resource12 kXrayPso12{0x5843u, 1};

// Supported OGF subset: format version 4, MT_NORMAL, FVF 0x112, inline
// vertices/indices, uint16 indices. Unknown/unsupported chunks are rejected.
bool LoadXrayOgf12(const std::wstring& path,
                   XrayAssetData12& asset,
                   const char** reason);

// Supported DDS subset: BC1/DXT1, 2D, one array slice, optional mip chain.
bool LoadXrayDds12(const std::wstring& path,
                   XrayTextureData12& texture,
                   const char** reason);

// CPU-only validation used both by the DDS loader and by the owner-side GPU
// upload. It checks actual source bytes, BC1 mip geometry and copy footprints
// separately; no GPU resource is published on failure.
bool ValidateXrayTextureSource(const XrayTextureData12& texture,
                               const char** reason);
bool ValidateXrayTextureCopy(const XrayTextureData12& texture,
                             const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts,
                             const UINT* rowCounts,
                             const UINT64* rowSizes,
                             uint32_t count,
                             uint64_t uploadBytes,
                             const char** reason);

// Owner-side transactional resolution for the single fixed asset slot.
// VBV/IBV are passed explicitly so CPU-only tests do not need a GPU.
bool ResolveXrayFrame(const renderprep12::OrderedFrame12& frame,
                      uint64_t liveGeneration,
                      const D3D12_VERTEX_BUFFER_VIEW& vertexView,
                      const D3D12_INDEX_BUFFER_VIEW& indexView,
                      uint32_t expectedIndexCount,
                      ResolvedFrame12& resolved,
                      const char** reason);

bool RunXrayAssetTests(const char* ogfPath, const char* ddsPath);
