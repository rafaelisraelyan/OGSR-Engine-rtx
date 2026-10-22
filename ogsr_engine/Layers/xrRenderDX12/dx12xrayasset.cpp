#include "stdafx.h"
#include "dx12xrayasset.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace
{
constexpr uint32_t kOgfHeader = 1;
constexpr uint32_t kOgfTexture = 2;
constexpr uint32_t kOgfVertices = 3;
constexpr uint32_t kOgfIndices = 4;
constexpr uint32_t kOgfVersion = 4;
constexpr uint32_t kOgfNormal = 0;
constexpr uint32_t kFvfXyzNormalTex1 = 0x112;
constexpr uint32_t kXrayVertexStride = 32;
constexpr size_t kMaxOgfBytes = 64u * 1024u * 1024u;
constexpr uint32_t kMaxVertices = 4u * 1024u * 1024u;
constexpr uint32_t kMaxIndices = 16u * 1024u * 1024u;

constexpr uint32_t kDdsMagic = 0x20534444u;
constexpr uint32_t kDdsHeaderSize = 124;
constexpr uint32_t kDdsdCaps = 0x1;
constexpr uint32_t kDdsdHeight = 0x2;
constexpr uint32_t kDdsdWidth = 0x4;
constexpr uint32_t kDdsdPixelFormat = 0x1000;
constexpr uint32_t kDdsdMipMapCount = 0x20000;
constexpr uint32_t kDdpfFourCC = 0x4;
constexpr uint32_t kDdscapsTexture = 0x1000;
constexpr uint32_t kDdscaps2CubeMapMask = 0xFE00;
constexpr uint32_t kDdscaps2Volume = 0x200000;
constexpr uint32_t kMaxTextureDimension = 16384;
constexpr uint32_t kMaxTextureMips = 16;

bool Fail(const char** reason, const char* text)
{
    if (reason)
        *reason = text;
    return false;
}

uint32_t ReadU32(const uint8_t* p)
{
    uint32_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

uint16_t ReadU16(const uint8_t* p)
{
    uint16_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

float ReadF32(const uint8_t* p)
{
    float value = 0.0f;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

bool Finite(float value)
{
    return std::isfinite(value);
}

bool ReadBytes(const std::wstring& path, std::vector<uint8_t>& bytes,
               size_t maxBytes, const char** reason)
{
    if (path.empty())
        return Fail(reason, "empty file path");
    std::ifstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::ate);
    if (!file)
        return Fail(reason, "cannot open file");
    const std::streamoff length = file.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > maxBytes)
        return Fail(reason, "file is empty or too large");
    bytes.resize(static_cast<size_t>(length));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length))
        return Fail(reason, "file read failed");
    return true;
}

bool ParseOgfTexture(const uint8_t* data, size_t size,
                     std::string& textureName, std::string& shaderName,
                     const char** reason)
{
    if (!data || size < 2)
        return Fail(reason, "invalid OGF_TEXTURE chunk");

    size_t first = 0;
    while (first < size && data[first] != 0)
        ++first;
    if (first >= size || first == 0)
        return Fail(reason, "missing texture name");

    const size_t secondStart = first + 1;
    size_t second = secondStart;
    while (second < size && data[second] != 0)
        ++second;
    if (second >= size)
        return Fail(reason, "missing shader name terminator");

    textureName.assign(reinterpret_cast<const char*>(data), first);
    shaderName.assign(reinterpret_cast<const char*>(data + secondStart),
                      second - secondStart);
    return true;
}
} // namespace

bool LoadXrayOgf12(const std::wstring& path, XrayAssetData12& asset,
                   const char** reason)
{
    asset = {};
    std::vector<uint8_t> file;
    if (!ReadBytes(path, file, kMaxOgfBytes, reason))
        return false;

    bool hasHeader = false;
    bool hasTexture = false;
    bool hasVertices = false;
    bool hasIndices = false;
    uint32_t headerType = 0;
    DirectX::XMFLOAT3 headerMin{};
    DirectX::XMFLOAT3 headerMax{};
    float headerRadius = 0.0f;

    size_t offset = 0;
    while (offset + 8 <= file.size())
    {
        const uint32_t id = ReadU32(file.data() + offset);
        const uint32_t chunkSize = ReadU32(file.data() + offset + 4);
        const uint64_t dataStart = static_cast<uint64_t>(offset) + 8;
        const uint64_t dataEnd = dataStart + chunkSize;
        if (dataEnd > file.size())
            return Fail(reason, "OGF chunk exceeds file size");
        const uint8_t* data = file.data() + dataStart;

        switch (id)
        {
        case kOgfHeader:
            if (hasHeader)
                return Fail(reason, "duplicate OGF_HEADER");
            if (chunkSize < 44)
                return Fail(reason, "short OGF_HEADER");
            if (data[0] != kOgfVersion)
                return Fail(reason, "unsupported OGF version");
            headerType = data[1];
            if (headerType != kOgfNormal)
                return Fail(reason, "OGF is not MT_NORMAL");
            headerMin = {ReadF32(data + 4), ReadF32(data + 8), ReadF32(data + 12)};
            headerMax = {ReadF32(data + 16), ReadF32(data + 20), ReadF32(data + 24)};
            headerRadius = ReadF32(data + 40);
            if (!Finite(headerMin.x) || !Finite(headerMin.y) || !Finite(headerMin.z) ||
                !Finite(headerMax.x) || !Finite(headerMax.y) || !Finite(headerMax.z) ||
                !Finite(headerRadius) || headerRadius <= 0.0f ||
                headerMin.x > headerMax.x || headerMin.y > headerMax.y ||
                headerMin.z > headerMax.z)
                return Fail(reason, "invalid OGF header bounds");
            hasHeader = true;
            break;

        case kOgfTexture:
            if (hasTexture)
                return Fail(reason, "duplicate OGF_TEXTURE");
            if (!ParseOgfTexture(data, chunkSize, asset.textureName,
                                 asset.shaderName, reason))
                return false;
            hasTexture = true;
            break;

        case kOgfVertices:
        {
            if (hasVertices)
                return Fail(reason, "duplicate OGF_VERTICES");
            if (chunkSize < 8)
                return Fail(reason, "short OGF_VERTICES");
            const uint32_t fvf = ReadU32(data);
            const uint32_t count = ReadU32(data + 4);
            if (fvf != kFvfXyzNormalTex1)
                return Fail(reason, "unsupported FVF (expected 0x112)");
            if (count == 0 || count > kMaxVertices)
                return Fail(reason, "invalid vertex count");
            const uint64_t required = 8ull +
                static_cast<uint64_t>(count) * kXrayVertexStride;
            if (required > chunkSize)
                return Fail(reason, "vertex data exceeds chunk");
            asset.vertices.resize(count);
            std::memcpy(asset.vertices.data(), data + 8,
                        static_cast<size_t>(count) * kXrayVertexStride);
            hasVertices = true;
            break;
        }

        case kOgfIndices:
        {
            if (hasIndices)
                return Fail(reason, "duplicate OGF_INDICES");
            if (chunkSize < 4)
                return Fail(reason, "short OGF_INDICES");
            const uint32_t count = ReadU32(data);
            if (count == 0 || count > kMaxIndices || (count % 3) != 0)
                return Fail(reason, "invalid index count");
            const uint64_t required = 4ull + static_cast<uint64_t>(count) * 2ull;
            if (required > chunkSize)
                return Fail(reason, "index data exceeds chunk");
            asset.indices.resize(count);
            std::memcpy(asset.indices.data(), data + 4,
                        static_cast<size_t>(count) * 2);
            hasIndices = true;
            break;
        }

        default:
            // F3.6A intentionally accepts only the inline MT_NORMAL subset.
            // GCONTAINER, FASTPATH, hierarchy and skeleton chunks are rejected.
            return Fail(reason, "unsupported OGF chunk");
        }

        offset = static_cast<size_t>(dataEnd);
    }

    if (offset != file.size())
        return Fail(reason, "trailing bytes after OGF chunks");
    if (!hasHeader || !hasTexture || !hasVertices || !hasIndices)
        return Fail(reason, "missing required OGF chunk");
    if (asset.textureName.empty())
        return Fail(reason, "empty texture name");

    DirectX::XMFLOAT3 minimum = asset.vertices[0].position;
    DirectX::XMFLOAT3 maximum = asset.vertices[0].position;
    for (const XrayVertex12& vertex : asset.vertices)
    {
        const DirectX::XMFLOAT3& p = vertex.position;
        if (!Finite(p.x) || !Finite(p.y) || !Finite(p.z) ||
            !Finite(vertex.normal.x) || !Finite(vertex.normal.y) ||
            !Finite(vertex.normal.z) || !Finite(vertex.uv.x) ||
            !Finite(vertex.uv.y))
            return Fail(reason, "non-finite vertex data");

        const float normalLength = std::sqrt(
            vertex.normal.x * vertex.normal.x +
            vertex.normal.y * vertex.normal.y +
            vertex.normal.z * vertex.normal.z);
        if (!Finite(normalLength) || std::fabs(normalLength - 1.0f) > 0.02f)
            return Fail(reason, "non-unit vertex normal");

        minimum.x = (std::min)(minimum.x, p.x);
        minimum.y = (std::min)(minimum.y, p.y);
        minimum.z = (std::min)(minimum.z, p.z);
        maximum.x = (std::max)(maximum.x, p.x);
        maximum.y = (std::max)(maximum.y, p.y);
        maximum.z = (std::max)(maximum.z, p.z);
    }
    if (minimum.x > maximum.x || minimum.y > maximum.y ||
        minimum.z > maximum.z)
        return Fail(reason, "invalid computed bounds");

    for (uint16_t index : asset.indices)
    {
        if (index >= asset.vertices.size())
            return Fail(reason, "index outside vertex range");
    }

    asset.minimum = minimum;
    asset.maximum = maximum;
    asset.sphere.center = {
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f};
    float radius = 0.0f;
    for (const XrayVertex12& vertex : asset.vertices)
    {
        const float dx = vertex.position.x - asset.sphere.center.x;
        const float dy = vertex.position.y - asset.sphere.center.y;
        const float dz = vertex.position.z - asset.sphere.center.z;
        radius = (std::max)(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    if (!Finite(radius) || radius <= 0.0f)
        return Fail(reason, "degenerate asset bounds");
    asset.sphere.radius = radius;
    return true;
}

bool ComputeTextureMipLayout(DXGI_FORMAT format, uint32_t width,
                             uint32_t height, uint32_t& rowPitch,
                             uint32_t& rows, uint32_t& dataSize)
{
    if (!width || !height)
        return false;
    uint64_t pitch = 0;
    uint64_t rowCount = 0;
    if (format == DXGI_FORMAT_BC1_UNORM)
    {
        pitch = (static_cast<uint64_t>(width) + 3ull) / 4ull * 8ull;
        rowCount = (static_cast<uint64_t>(height) + 3ull) / 4ull;
    }
    else if (format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        pitch = static_cast<uint64_t>(width) * 4ull;
        rowCount = height;
    }
    else
    {
        return false;
    }
    const uint64_t size = pitch * rowCount;
    if (pitch > UINT32_MAX || rowCount > UINT32_MAX || size > UINT32_MAX)
        return false;
    rowPitch = static_cast<uint32_t>(pitch);
    rows = static_cast<uint32_t>(rowCount);
    dataSize = static_cast<uint32_t>(size);
    return true;
}

uint32_t MaximumMipCount(uint32_t width, uint32_t height)
{
    uint32_t dimension = (std::max)(width, height);
    uint32_t count = 1;
    while (dimension > 1)
    {
        dimension >>= 1;
        ++count;
    }
    return count;
}

bool ValidateXrayTextureSource(const XrayTextureData12& texture,
                               const char** reason)
{
    if (texture.data.empty() || texture.mipCount == 0 ||
        texture.mips.size() != texture.mipCount ||
        texture.mipCount > kMaxTextureMips)
        return Fail(reason, "invalid DDS mip metadata");
    if (!texture.width || !texture.height ||
        texture.width > kMaxTextureDimension ||
        texture.height > kMaxTextureDimension ||
        texture.mipCount > MaximumMipCount(texture.width, texture.height))
        return Fail(reason, "invalid DDS dimensions/mips");
    if (texture.format != DXGI_FORMAT_BC1_UNORM &&
        texture.format != DXGI_FORMAT_R8G8B8A8_UNORM)
        return Fail(reason, "unsupported DDS texture format");

    uint32_t mipWidth = texture.width;
    uint32_t mipHeight = texture.height;
    for (uint32_t mip = 0; mip < texture.mipCount; ++mip)
    {
        uint32_t rowPitch = 0;
        uint32_t rows = 0;
        uint32_t dataSize = 0;
        if (!ComputeTextureMipLayout(texture.format, mipWidth, mipHeight,
                                     rowPitch, rows, dataSize))
            return Fail(reason, "invalid DDS mip geometry");
        const XrayTextureMip12& source = texture.mips[mip];
        if (source.width != mipWidth || source.height != mipHeight ||
            source.rowPitch != rowPitch || source.dataSize != dataSize ||
            source.dataOffset > texture.data.size() ||
            source.dataSize > texture.data.size() - source.dataOffset)
            return Fail(reason, "DDS mip metadata exceeds source data");
        if (mip + 1 < texture.mipCount)
        {
            mipWidth = (std::max)(1u, mipWidth / 2u);
            mipHeight = (std::max)(1u, mipHeight / 2u);
        }
    }
    return true;
}

bool ValidateXrayTextureCopy(const XrayTextureData12& texture,
                             const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts,
                             const UINT* rowCounts,
                             const UINT64* rowSizes,
                             uint32_t count,
                             uint64_t uploadBytes,
                             const char** reason)
{
    if (!layouts || !rowCounts || !rowSizes || !uploadBytes ||
        count != texture.mipCount ||
        !ValidateXrayTextureSource(texture, reason))
        return false;

    uint32_t mipWidth = texture.width;
    uint32_t mipHeight = texture.height;
    for (uint32_t mip = 0; mip < count; ++mip)
    {
        uint32_t expectedRowPitch = 0;
        uint32_t expectedRows = 0;
        uint32_t expectedDataSize = 0;
        if (!ComputeTextureMipLayout(texture.format, mipWidth, mipHeight,
                                     expectedRowPitch, expectedRows,
                                     expectedDataSize))
            return Fail(reason, "invalid destination mip geometry");
        const XrayTextureMip12& source = texture.mips[mip];
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[mip];
        // D3D12 reports BC footprints on block boundaries; sub-4 mips are
        // represented by one 4x4 block, not by their logical 2x2/1x1 size.
        const uint32_t footprintWidth =
            texture.format == DXGI_FORMAT_BC1_UNORM
                ? ((mipWidth + 3u) / 4u) * 4u : mipWidth;
        const uint32_t footprintHeight =
            texture.format == DXGI_FORMAT_BC1_UNORM
                ? ((mipHeight + 3u) / 4u) * 4u : mipHeight;
        if (layout.Footprint.Format != texture.format ||
            layout.Footprint.Width != footprintWidth ||
            layout.Footprint.Height != footprintHeight ||
            layout.Footprint.Depth != 1 ||
            source.rowPitch != expectedRowPitch ||
            source.dataSize != expectedDataSize ||
            rowCounts[mip] != expectedRows ||
            rowSizes[mip] != expectedRowPitch ||
            layout.Footprint.RowPitch < rowSizes[mip] ||
            layout.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0 ||
            layout.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0)
            return Fail(reason, "invalid D3D12 copy footprint");

        if (layout.Offset > uploadBytes)
            return Fail(reason, "D3D12 copy offset exceeds upload buffer");
        const uint64_t pitch = layout.Footprint.RowPitch;
        const uint64_t rows = rowCounts[mip];
        if (!pitch || !rows)
            return Fail(reason, "invalid D3D12 copy rows/pitch");
        const uint64_t lastRowOffset = (rows - 1ull) * pitch;
        if (lastRowOffset > uploadBytes - layout.Offset ||
            rowSizes[mip] > uploadBytes - layout.Offset - lastRowOffset)
            return Fail(reason, "D3D12 copy range exceeds upload buffer");

        if (mip + 1 < count)
        {
            mipWidth = (std::max)(1u, mipWidth / 2u);
            mipHeight = (std::max)(1u, mipHeight / 2u);
        }
    }
    return true;
}

bool LoadXrayDds12(const std::wstring& path, XrayTextureData12& texture,
                   const char** reason)
{
    texture = {};
    std::vector<uint8_t> file;
    if (!ReadBytes(path, file, 64u * 1024u * 1024u, reason))
        return false;
    if (file.size() < 128 || ReadU32(file.data()) != kDdsMagic)
        return Fail(reason, "invalid DDS magic");
    if (ReadU32(file.data() + 4) != kDdsHeaderSize)
        return Fail(reason, "invalid DDS header size");

    const uint32_t flags = ReadU32(file.data() + 8);
    const uint32_t height = ReadU32(file.data() + 12);
    const uint32_t width = ReadU32(file.data() + 16);
    const uint32_t depth = ReadU32(file.data() + 24);
    const uint32_t mipField = ReadU32(file.data() + 28);
    const uint32_t mipCount = (std::max)(1u, mipField);
    const uint32_t pixelFlags = ReadU32(file.data() + 80);
    const uint32_t caps = ReadU32(file.data() + 108);
    const uint32_t caps2 = ReadU32(file.data() + 112);

    const uint32_t requiredFlags = kDdsdCaps | kDdsdHeight |
        kDdsdWidth | kDdsdPixelFormat;
    if ((flags & requiredFlags) != requiredFlags)
        return Fail(reason, "DDS header misses required flags");
    if (mipCount > 1 && !(flags & kDdsdMipMapCount))
        return Fail(reason, "DDS mip chain has no MIPMAPCOUNT flag");
    if (depth > 1)
        return Fail(reason, "DDS volume textures are unsupported");
    if ((pixelFlags & kDdpfFourCC) == 0)
        return Fail(reason, "DDS pixel format is not FOURCC");
    if (ReadU32(file.data() + 76) != 32 ||
        std::memcmp(file.data() + 84, "DXT1", 4) != 0)
        return Fail(reason, "DDS is not legacy BC1/DXT1");
    if ((caps & kDdscapsTexture) == 0)
        return Fail(reason, "DDS is not a texture");
    if (caps2 & (kDdscaps2CubeMapMask | kDdscaps2Volume))
        return Fail(reason, "DDS cube/volume textures are unsupported");
    if (!width || !height || width > kMaxTextureDimension ||
        height > kMaxTextureDimension || mipCount > kMaxTextureMips ||
        mipCount > MaximumMipCount(width, height))
        return Fail(reason, "invalid DDS dimensions/mips");

    texture.width = width;
    texture.height = height;
    texture.mipCount = mipCount;
    texture.format = DXGI_FORMAT_BC1_UNORM;
    texture.mips.reserve(mipCount);

    const uint64_t payloadBytes = file.size() - 128ull;
    uint64_t offset = 0;
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    for (uint32_t mip = 0; mip < mipCount; ++mip)
    {
        XrayTextureMip12 info{};
        uint32_t rows = 0;
        uint32_t dataSize = 0;
        if (!ComputeTextureMipLayout(DXGI_FORMAT_BC1_UNORM, mipWidth,
                                     mipHeight, info.rowPitch, rows, dataSize))
            return Fail(reason, "DDS mip geometry overflow");
        info.width = mipWidth;
        info.height = mipHeight;
        info.dataSize = dataSize;
        if (offset > payloadBytes ||
            static_cast<uint64_t>(info.dataSize) > payloadBytes - offset ||
            offset > UINT32_MAX)
            return Fail(reason, "DDS mip data exceeds file");
        info.dataOffset = static_cast<uint32_t>(offset);
        texture.mips.push_back(info);
        offset += info.dataSize;
        if (mip + 1 < mipCount)
        {
            mipWidth = (std::max)(1u, mipWidth / 2u);
            mipHeight = (std::max)(1u, mipHeight / 2u);
        }
    }
    if (offset > payloadBytes || offset > UINT32_MAX)
        return Fail(reason, "DDS mip data exceeds file");
    texture.data.assign(file.begin() + 128, file.begin() + 128 + offset);
    return ValidateXrayTextureSource(texture, reason);
}

bool ResolveXrayFrame(const renderprep12::OrderedFrame12& frame,
                      uint64_t liveGeneration,
                      const D3D12_VERTEX_BUFFER_VIEW& vertexView,
                      const D3D12_INDEX_BUFFER_VIEW& indexView,
                      uint32_t expectedIndexCount,
                      ResolvedFrame12& resolved,
                      const char** reason)
{
    auto fail = [reason](const char* text)
    {
        if (reason)
            *reason = text;
        return false;
    };

    if (!liveGeneration || frame.frame.generation != liveGeneration)
        return fail("stale X-Ray frame generation");
    if (frame.categories[1].count != 0 || frame.categories[2].count != 0)
        return fail("transparent/hud packets are unsupported");
    if (frame.categories[0].begin != 0 ||
        frame.categories[0].count != frame.frame.packets.size())
        return fail("invalid opaque category range");
    if (frame.frame.packets.empty() ||
        frame.frame.packets.size() > dx12object12::kMaxObjects)
        return fail("invalid X-Ray object count");
    if (!expectedIndexCount || expectedIndexCount % 3 != 0 ||
        indexView.Format != DXGI_FORMAT_R16_UINT ||
        vertexView.StrideInBytes != 32 ||
        vertexView.SizeInBytes < 32 ||
        static_cast<uint64_t>(indexView.SizeInBytes) <
            static_cast<uint64_t>(expectedIndexCount) * 2ull)
        return fail("invalid X-Ray GPU views");

    ResolvedFrame12 candidate;
    candidate.generation = frame.frame.generation;
    candidate.draws.resize(frame.frame.packets.size());
    for (size_t i = 0; i < frame.frame.packets.size(); ++i)
    {
        const renderprep12::DrawPacket12& packet = frame.frame.packets[i];
        if (!packet.visible)
            return fail("hidden packet in ordered X-Ray frame");
        if (packet.object.visual != kXrayVisual12 ||
            packet.object.material != kXrayMaterial12 ||
            packet.object.pso != kXrayPso12)
            return fail("stale or unsupported X-Ray resource handle");
        if (packet.object.firstVertex != 0 || packet.object.vertexCount != 0 ||
            packet.object.firstIndex != 0 ||
            packet.object.indexCount != expectedIndexCount ||
            packet.object.baseVertex != 0)
            return fail("invalid X-Ray index range");
        if (!dx12object12::IsValidTransformWorld(packet.object.world))
            return fail("invalid X-Ray world transform");
        if (packet.object.flags & (renderprep12::Hud | renderprep12::Transparent) ||
            packet.object.opacity != 1.0f)
            return fail("unsupported X-Ray material flags");

        GpuDraw12& draw = candidate.draws[i];
        draw.meshIndex = 0;
        draw.pipeline = 1;
        draw.objectConstantIndex = static_cast<uint32_t>(i);
        draw.vertexView = vertexView;
        draw.indexView = indexView;
        draw.firstIndex = 0;
        draw.indexCount = expectedIndexCount;
        draw.baseVertex = 0;
    }
    resolved = std::move(candidate);
    return true;
}
