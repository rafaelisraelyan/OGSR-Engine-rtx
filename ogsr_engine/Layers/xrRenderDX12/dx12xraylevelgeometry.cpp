#include "dx12xraylevelgeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <utility>

namespace xraylevel12
{
const char* ReadErrorMessage(ReadError error) noexcept
{
    switch (error)
    {
    case ReadError::None: return "";
    case ReadError::EmptyLevelInput: return "empty level input";
    case ReadError::EmptyGeomInput: return "empty level.geom input";
    case ReadError::LevelInputTooLarge: return "level input exceeds size limit";
    case ReadError::GeomInputTooLarge: return "level.geom input exceeds size limit";
    case ReadError::InputOutputAliasing: return "input overlaps output or reason storage";
    case ReadError::CompressedChunk: return "compressed chunk flags are not supported";
    case ReadError::TruncatedChunkHeader: return "truncated chunk header";
    case ReadError::ChunkExceedsInput: return "chunk exceeds input";
    case ReadError::DuplicateMandatoryChunk: return "duplicate mandatory chunk";
    case ReadError::MissingMandatoryChunk: return "missing mandatory chunk";
    case ReadError::TruncatedVisualChunkHeader: return "truncated visual chunk header";
    case ReadError::VisualChunkExceedsContainer: return "visual chunk exceeds visuals container";
    case ReadError::DuplicateVisualId: return "duplicate visual id";
    case ReadError::VisualNotFound: return "requested visual id not found";
    case ReadError::TruncatedVisualSubchunkHeader: return "truncated visual chunk header";
    case ReadError::VisualSubchunkExceedsVisual: return "visual subchunk exceeds visual";
    case ReadError::UnsupportedOgfVersion: return "unsupported OGF version";
    case ReadError::UnsupportedVisualType: return "unsupported visual type";
    case ReadError::InvalidOgfHeaderSize: return "invalid OGF_HEADER size";
    case ReadError::TruncatedOgfHeader: return "truncated OGF_HEADER";
    case ReadError::TruncatedOgfGcContainer: return "truncated OGF_GCONTAINER";
    case ReadError::NonFiniteOgfHeaderBounds: return "non-finite OGF_HEADER bounds";
    case ReadError::InvalidOgfHeaderBounds: return "invalid OGF_HEADER bounds";
    case ReadError::DuplicateOgfHeader: return "duplicate OGF_HEADER";
    case ReadError::MissingOgfHeader: return "missing OGF_HEADER";
    case ReadError::DuplicateOgfGcContainer: return "duplicate OGF_GCONTAINER";
    case ReadError::MissingOgfGcContainer: return "missing OGF_GCONTAINER";
    case ReadError::InvalidOgfGcContainerSize: return "invalid OGF_GCONTAINER size";
    case ReadError::DuplicateOgfFastPath: return "duplicate OGF_FASTPATH";
    case ReadError::UnsupportedOgfSubchunk: return "unsupported OGF subchunk";
    case ReadError::TruncatedVbContainer: return "truncated VB container count";
    case ReadError::InvalidVbContainerCount: return "invalid VB container count";
    case ReadError::TruncatedVbCount: return "truncated VB vertex count";
    case ReadError::InvalidVbCountOrStride: return "invalid VB vertex count/stride";
    case ReadError::VbPayloadExceedsContainer: return "VB payload exceeds container";
    case ReadError::TrailingVbBytes: return "trailing bytes in VB container";
    case ReadError::TruncatedIbContainer: return "truncated IB container count";
    case ReadError::InvalidIbContainerCount: return "invalid IB container count";
    case ReadError::TruncatedIbCount: return "truncated IB index count";
    case ReadError::InvalidIbCount: return "invalid IB index count";
    case ReadError::IbPayloadExceedsContainer: return "IB payload exceeds container";
    case ReadError::TrailingIbBytes: return "trailing bytes in IB container";
    case ReadError::InvalidSourceVbId: return "invalid source VB id";
    case ReadError::InvalidSourceIbId: return "invalid source IB id";
    case ReadError::UnsupportedDeclaration: return "unsupported vertex declaration";
    case ReadError::TruncatedDeclaration: return "truncated vertex declaration";
    case ReadError::InvalidDeclarationTerminator: return "invalid vertex declaration terminator";
    case ReadError::InvalidDeclarationTerminatorFields: return "invalid D3DDECL_END fields";
    case ReadError::UnsupportedDeclarationElement: return "unsupported vertex declaration element";
    case ReadError::VertexElementExceedsStride: return "vertex element exceeds stride limit";
    case ReadError::InvalidVisualCounts: return "invalid visual counts";
    case ReadError::VertexRangeOutsideVb: return "visual vertex range outside source VB";
    case ReadError::IndexRangeOutsideIb: return "visual index range outside source IB";
    case ReadError::OutputBudgetExceeded: return "output geometry exceeds size limit";
    case ReadError::VertexRangeOutsideGeom: return "source vertex range exceeds level.geom";
    case ReadError::IndexRangeOutsideGeom: return "source index range exceeds level.geom";
    case ReadError::TruncatedPosition: return "truncated position data";
    case ReadError::NonFinitePosition: return "non-finite position data";
    case ReadError::ComputedBoundsOutsideHeader: return "computed bounds exceed header bounds";
    case ReadError::InvalidComputedSphere: return "invalid computed sphere";
    case ReadError::TruncatedIndex: return "truncated index data";
    case ReadError::IndexOutsideVertexRange: return "index outside compact vertex range";
    case ReadError::AllocationFailure: return "allocation failure";
    case ReadError::UnexpectedException: return "unexpected exception";
    }
    return "unknown reader error";
}

namespace
{
constexpr uint32_t kD3dDeclTypeFloat3 = 2;
constexpr uint32_t kD3dDeclTypeD3dColor = 4;
constexpr uint32_t kD3dDeclTypeShort2 = 6;
constexpr uint32_t kD3dDeclTypeShort4 = 7;
constexpr uint32_t kD3dDeclUsagePosition = 0;
constexpr uint32_t kD3dDeclUsageNormal = 3;
constexpr uint32_t kD3dDeclUsageTexcoord = 5;
constexpr uint32_t kD3dDeclUsageTangent = 6;
constexpr uint32_t kD3dDeclUsageBinormal = 7;
constexpr uint32_t kD3dDeclUsageColor = 10;
constexpr uint32_t kSelectedDeclarationCount = 7; // шесть элементов + terminator
constexpr uint32_t kSelectedStride = 32;
constexpr uint32_t kMaxStride = 256;

struct Chunk
{
    uint32_t id = 0;
    uint64_t payload = 0;
    uint32_t size = 0;
};

struct VbInfo
{
    uint32_t id = 0;
    uint32_t count = 0;
    uint32_t stride = 0;
    uint64_t data_offset = 0;
    uint32_t declaration_count = 0;
    std::array<DeclarationElement, kMaxDeclarationElements> declaration{};
};

struct IbInfo
{
    uint32_t id = 0;
    uint32_t count = 0;
    uint64_t data_offset = 0;
};

void SetReasonBestEffort(std::string& reason, ReadError error) noexcept
{
    try
    {
        if (error == ReadError::None)
            reason.clear();
        else
            reason = ReadErrorMessage(error);
    }
    catch (...)
    {
        // Reason — только диагностика. Status code остаётся валидным, даже
        // если процесс не может выделить строку.
    }
}

ReadError Fail(std::string& reason, ReadError error) noexcept
{
    SetReasonBestEffort(reason, error);
    return error;
}

bool ReadU16(std::span<const uint8_t> bytes, uint64_t offset, uint16_t& value) noexcept
{
    if (offset > bytes.size() || bytes.size() - static_cast<size_t>(offset) < 2)
        return false;
    value = static_cast<uint16_t>(bytes[static_cast<size_t>(offset)]) |
            static_cast<uint16_t>(bytes[static_cast<size_t>(offset + 1)] << 8);
    return true;
}

bool ReadU32(std::span<const uint8_t> bytes, uint64_t offset, uint32_t& value) noexcept
{
    if (offset > bytes.size() || bytes.size() - static_cast<size_t>(offset) < 4)
        return false;
    const size_t p = static_cast<size_t>(offset);
    value = static_cast<uint32_t>(bytes[p]) |
            (static_cast<uint32_t>(bytes[p + 1]) << 8) |
            (static_cast<uint32_t>(bytes[p + 2]) << 16) |
            (static_cast<uint32_t>(bytes[p + 3]) << 24);
    return true;
}

bool ReadF32(std::span<const uint8_t> bytes, uint64_t offset, float& value) noexcept
{
    uint32_t bits = 0;
    if (!ReadU32(bytes, offset, bits))
        return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool RangesOverlap(const void* a, uint64_t a_size,
                   const void* b, uint64_t b_size) noexcept
{
    if (a == nullptr || b == nullptr || a_size == 0 || b_size == 0)
        return false;
    const uintptr_t ap = reinterpret_cast<uintptr_t>(a);
    const uintptr_t bp = reinterpret_cast<uintptr_t>(b);
    if (ap <= bp)
        return bp - ap < a_size;
    return ap - bp < b_size;
}

bool InputsAliasOutput(std::span<const uint8_t> level,
                       std::span<const uint8_t> geom,
                       const Geometry& output,
                       const std::string& reason) noexcept
{
    const auto overlaps_output_object = [&](const void* p, uint64_t n) {
        return RangesOverlap(p, n, &output, sizeof(output)) ||
               RangesOverlap(p, n, &reason, sizeof(reason));
    };
    const auto overlaps_buffer = [&](const void* p, uint64_t n,
                                     const void* data, uint64_t capacity) {
        return RangesOverlap(p, n, data, capacity);
    };

    const uint8_t* level_p = level.data();
    const uint64_t level_n = level.size();
    const uint8_t* geom_p = geom.data();
    const uint64_t geom_n = geom.size();
    const uint64_t indices_capacity_bytes =
        static_cast<uint64_t>(output.indices.capacity()) * sizeof(uint16_t);

    if (overlaps_output_object(level_p, level_n) ||
        overlaps_output_object(geom_p, geom_n) ||
        overlaps_buffer(level_p, level_n, output.vertex_bytes.data(),
                        output.vertex_bytes.capacity()) ||
        overlaps_buffer(level_p, level_n, output.indices.data(),
                        indices_capacity_bytes) ||
        overlaps_buffer(level_p, level_n, reason.data(), reason.capacity()) ||
        overlaps_buffer(geom_p, geom_n, output.vertex_bytes.data(),
                        output.vertex_bytes.capacity()) ||
        overlaps_buffer(geom_p, geom_n, output.indices.data(),
                        indices_capacity_bytes) ||
        overlaps_buffer(geom_p, geom_n, reason.data(), reason.capacity()))
        return true;
    return false;
}

ReadError FindUniqueChunk(std::span<const uint8_t> bytes, uint32_t id,
                          Chunk& found, std::string& reason)
{
    bool present = false;
    uint64_t pos = 0;
    while (pos < bytes.size())
    {
        uint32_t type = 0;
        uint32_t size = 0;
        if (bytes.size() - static_cast<size_t>(pos) < 8)
            return Fail(reason, ReadError::TruncatedChunkHeader);
        if (!ReadU32(bytes, pos, type) || !ReadU32(bytes, pos + 4, size))
            return Fail(reason, ReadError::TruncatedChunkHeader);
        if ((type & kCompressMark) != 0)
            return Fail(reason, ReadError::CompressedChunk);
        const uint64_t payload = pos + 8;
        if (size > bytes.size() - static_cast<size_t>(payload))
            return Fail(reason, ReadError::ChunkExceedsInput);
        if (type == id)
        {
            if (present)
                return Fail(reason, ReadError::DuplicateMandatoryChunk);
            present = true;
            found.id = type;
            found.payload = payload;
            found.size = size;
        }
        pos = payload + size;
    }
    if (!present)
        return Fail(reason, ReadError::MissingMandatoryChunk);
    return ReadError::None;
}

ReadError FindVisual(std::span<const uint8_t> level, uint32_t visual_id,
                     Chunk& visual, uint64_t& source_offset, std::string& reason)
{
    Chunk visuals{};
    ReadError error = FindUniqueChunk(level, kLevelVisualsChunk, visuals, reason);
    if (error != ReadError::None)
        return error;

    bool present = false;
    uint64_t pos = visuals.payload;
    const uint64_t end = visuals.payload + visuals.size;
    while (pos < end)
    {
        uint32_t type = 0;
        uint32_t size = 0;
        if (end - pos < 8)
            return Fail(reason, ReadError::TruncatedVisualChunkHeader);
        if (!ReadU32(level, pos, type) || !ReadU32(level, pos + 4, size))
            return Fail(reason, ReadError::TruncatedVisualChunkHeader);
        if ((type & kCompressMark) != 0)
            return Fail(reason, ReadError::CompressedChunk);
        const uint64_t payload = pos + 8;
        if (size > end - payload)
            return Fail(reason, ReadError::VisualChunkExceedsContainer);
        if (type == visual_id)
        {
            if (present)
                return Fail(reason, ReadError::DuplicateVisualId);
            present = true;
            visual.id = type;
            visual.payload = payload;
            visual.size = size;
            source_offset = payload;
        }
        pos = payload + size;
    }
    if (!present)
        return Fail(reason, ReadError::VisualNotFound);
    return ReadError::None;
}

ReadError ParseVisualHeader(std::span<const uint8_t> level, const Chunk& chunk,
                            uint32_t& shader_id, Bounds& bounds,
                            std::string& reason)
{
    const uint64_t payload = chunk.payload;
    const uint64_t end = payload + chunk.size;
    bool has_header = false;
    bool has_fastpath = false;
    bool has_gc = false;

    uint64_t pos = payload;
    while (pos < end)
    {
        uint32_t type = 0;
        uint32_t size = 0;
        if (end - pos < 8)
            return Fail(reason, ReadError::TruncatedVisualSubchunkHeader);
        if (!ReadU32(level, pos, type) || !ReadU32(level, pos + 4, size))
            return Fail(reason, ReadError::TruncatedVisualSubchunkHeader);
        if ((type & kCompressMark) != 0)
            return Fail(reason, ReadError::CompressedChunk);
        const uint64_t data = pos + 8;
        if (size > end - data)
            return Fail(reason, ReadError::VisualSubchunkExceedsVisual);

        if (type == kOgfHeader)
        {
            if (has_header)
                return Fail(reason, ReadError::DuplicateOgfHeader);
            if (size != 44)
                return Fail(reason, ReadError::InvalidOgfHeaderSize);
            if (level[static_cast<size_t>(data)] != kOgfVersion)
                return Fail(reason, ReadError::UnsupportedOgfVersion);
            if (level[static_cast<size_t>(data + 1)] != kMtNormal)
                return Fail(reason, ReadError::UnsupportedVisualType);
            uint16_t parsed_shader_id = 0;
            if (!ReadU16(level, data + 2, parsed_shader_id))
                return Fail(reason, ReadError::TruncatedOgfHeader);
            shader_id = parsed_shader_id;
            for (int i = 0; i < 3; ++i)
            {
                if (!ReadF32(level, data + 4 + i * 4, bounds.minimum[i]) ||
                    !ReadF32(level, data + 16 + i * 4, bounds.maximum[i]))
                    return Fail(reason, ReadError::TruncatedOgfHeader);
                if (!std::isfinite(bounds.minimum[i]) ||
                    !std::isfinite(bounds.maximum[i]))
                    return Fail(reason, ReadError::NonFiniteOgfHeaderBounds);
            }
            if (bounds.minimum[0] > bounds.maximum[0] ||
                bounds.minimum[1] > bounds.maximum[1] ||
                bounds.minimum[2] > bounds.maximum[2])
                return Fail(reason, ReadError::InvalidOgfHeaderBounds);
            // Header sphere присутствует в формате исходника, но намеренно
            // не используется и не считается надёжной границей culling.
            has_header = true;
        }
        else if (type == kOgfFastPath)
        {
            if (has_fastpath)
                return Fail(reason, ReadError::DuplicateOgfFastPath);
            has_fastpath = true;
        }
        else if (type == kOgfGcContainer)
        {
            if (has_gc)
                return Fail(reason, ReadError::DuplicateOgfGcContainer);
            if (size != 24)
                return Fail(reason, ReadError::InvalidOgfGcContainerSize);
            has_gc = true;
        }
        else
        {
            return Fail(reason, ReadError::UnsupportedOgfSubchunk);
        }

        pos = data + size;
    }

    if (!has_header)
        return Fail(reason, ReadError::MissingOgfHeader);
    if (!has_gc)
        return Fail(reason, ReadError::MissingOgfGcContainer);
    return ReadError::None;
}

ReadError ParseContainer(std::span<const uint8_t> level, const Chunk& chunk,
                         uint32_t& vb_id, uint32_t& vbase, uint32_t& vcount,
                         uint32_t& ib_id, uint32_t& ibase, uint32_t& icount,
                         std::string& reason)
{
    const uint64_t end = chunk.payload + chunk.size;
    uint64_t pos = chunk.payload;
    while (pos < end)
    {
        uint32_t type = 0;
        uint32_t size = 0;
        if (end - pos < 8)
            return Fail(reason, ReadError::TruncatedVisualSubchunkHeader);
        if (!ReadU32(level, pos, type) || !ReadU32(level, pos + 4, size))
            return Fail(reason, ReadError::TruncatedVisualSubchunkHeader);
        if ((type & kCompressMark) != 0)
            return Fail(reason, ReadError::CompressedChunk);
        const uint64_t data = pos + 8;
        if (size > end - data)
            return Fail(reason, ReadError::VisualSubchunkExceedsVisual);
        if (type == kOgfGcContainer)
        {
            if (size != 24)
                return Fail(reason, ReadError::InvalidOgfGcContainerSize);
            if (!ReadU32(level, data + 0, vb_id) ||
                !ReadU32(level, data + 4, vbase) ||
                !ReadU32(level, data + 8, vcount) ||
                !ReadU32(level, data + 12, ib_id) ||
                !ReadU32(level, data + 16, ibase) ||
                !ReadU32(level, data + 20, icount))
                return Fail(reason, ReadError::TruncatedOgfGcContainer);
            return ReadError::None;
        }
        pos = data + size;
    }
    return Fail(reason, ReadError::MissingOgfGcContainer);
}

uint32_t DeclTypeSize(uint8_t type) noexcept
{
    static constexpr uint8_t sizes[] = {4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8};
    return type < std::size(sizes) ? sizes[type] : 0;
}

ReadError ParseDeclaration(std::span<const uint8_t> geom, uint64_t offset,
                           uint64_t limit,
                           std::array<DeclarationElement, kMaxDeclarationElements>& out,
                           uint32_t& count, uint32_t& stride, uint64_t& consumed,
                           std::string& reason)
{
    uint64_t p = offset;
    uint32_t n = 0;
    stride = 0;
    for (;;)
    {
        if (n >= kMaxDeclarationElements || p > limit || limit - p < 8)
            return Fail(reason, ReadError::InvalidDeclarationTerminator);
        DeclarationElement e;
        if (!ReadU16(geom, p, e.stream) ||
            !ReadU16(geom, p + 2, e.offset))
            return Fail(reason, ReadError::TruncatedDeclaration);
        e.type = geom[static_cast<size_t>(p + 4)];
        e.method = geom[static_cast<size_t>(p + 5)];
        e.usage = geom[static_cast<size_t>(p + 6)];
        e.usage_index = geom[static_cast<size_t>(p + 7)];
        out[n++] = e;
        p += 8;
        if (e.stream == 0xFF)
        {
            // D3DDECL_END обязан совпадать полностью:
            // { 0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0 }.
            if (e.offset != 0 || e.type != kD3dDeclTypeUnused ||
                e.method != 0 || e.usage != 0 || e.usage_index != 0)
                return Fail(reason, ReadError::InvalidDeclarationTerminatorFields);
            count = n;
            consumed = p - offset;
            return ReadError::None;
        }
        if (e.stream != 0 || e.method != 0 ||
            e.type >= kD3dDeclTypeUnused || DeclTypeSize(e.type) == 0)
            return Fail(reason, ReadError::UnsupportedDeclarationElement);
        const uint32_t element_end = static_cast<uint32_t>(e.offset) + DeclTypeSize(e.type);
        if (element_end > kMaxStride)
            return Fail(reason, ReadError::VertexElementExceedsStride);
        stride = std::max(stride, element_end);
    }
}

ReadError ParseGeometry(std::span<const uint8_t> geom,
                        std::vector<VbInfo>& vbs, std::vector<IbInfo>& ibs,
                        std::string& reason)
{
    Chunk vb_chunk{};
    Chunk ib_chunk{};
    ReadError error = FindUniqueChunk(geom, kLevelVertexChunk, vb_chunk, reason);
    if (error != ReadError::None)
        return error;
    error = FindUniqueChunk(geom, kLevelIndexChunk, ib_chunk, reason);
    if (error != ReadError::None)
        return error;

    uint32_t vb_count = 0;
    if (vb_chunk.size < 4 || !ReadU32(geom, vb_chunk.payload, vb_count))
        return Fail(reason, ReadError::TruncatedVbContainer);
    if (vb_count == 0 || vb_count > kMaxVertexBuffers)
        return Fail(reason, ReadError::InvalidVbContainerCount);

    const uint64_t vb_end = vb_chunk.payload + vb_chunk.size;
    uint64_t p = vb_chunk.payload + 4;
    vbs.clear();
    vbs.reserve(vb_count);
    for (uint32_t i = 0; i < vb_count; ++i)
    {
        VbInfo info;
        info.id = i;
        uint64_t consumed = 0;
        error = ParseDeclaration(geom, p, vb_end, info.declaration,
                                 info.declaration_count, info.stride, consumed, reason);
        if (error != ReadError::None)
            return error;
        p += consumed;
        if (p > vb_end || vb_end - p < 4)
            return Fail(reason, ReadError::TruncatedVbCount);
        if (!ReadU32(geom, p, info.count))
            return Fail(reason, ReadError::TruncatedVbCount);
        p += 4;
        if (info.count == 0 || info.count > kMaxVertexCount || info.stride == 0)
            return Fail(reason, ReadError::InvalidVbCountOrStride);
        const uint64_t bytes = static_cast<uint64_t>(info.count) * info.stride;
        if (bytes > vb_end - p)
            return Fail(reason, ReadError::VbPayloadExceedsContainer);
        info.data_offset = p;
        p += bytes;
        vbs.push_back(info);
    }
    if (p != vb_end)
        return Fail(reason, ReadError::TrailingVbBytes);

    uint32_t ib_count = 0;
    if (ib_chunk.size < 4 || !ReadU32(geom, ib_chunk.payload, ib_count))
        return Fail(reason, ReadError::TruncatedIbContainer);
    if (ib_count == 0 || ib_count > kMaxIndexBuffers)
        return Fail(reason, ReadError::InvalidIbContainerCount);

    const uint64_t ib_end = ib_chunk.payload + ib_chunk.size;
    uint64_t q = ib_chunk.payload + 4;
    ibs.clear();
    ibs.reserve(ib_count);
    for (uint32_t i = 0; i < ib_count; ++i)
    {
        IbInfo info;
        info.id = i;
        if (q > ib_end || ib_end - q < 4)
            return Fail(reason, ReadError::TruncatedIbCount);
        if (!ReadU32(geom, q, info.count))
            return Fail(reason, ReadError::TruncatedIbCount);
        q += 4;
        if (info.count == 0 || info.count > kMaxIndexCount)
            return Fail(reason, ReadError::InvalidIbCount);
        const uint64_t bytes = static_cast<uint64_t>(info.count) * 2ull;
        if (bytes > ib_end - q)
            return Fail(reason, ReadError::IbPayloadExceedsContainer);
        info.data_offset = q;
        q += bytes;
        ibs.push_back(info);
    }
    if (q != ib_end)
        return Fail(reason, ReadError::TrailingIbBytes);
    return ReadError::None;
}

bool IsSelectedDeclaration(const VbInfo& vb) noexcept
{
    if (vb.stride != kSelectedStride ||
        vb.declaration_count != kSelectedDeclarationCount)
        return false;

    const auto& d = vb.declaration;
    const auto match = [&](uint32_t i, uint16_t stream, uint16_t offset,
                           uint8_t type, uint8_t usage, uint8_t usage_index) {
        return d[i].stream == stream && d[i].offset == offset &&
               d[i].type == type && d[i].method == 0 &&
               d[i].usage == usage && d[i].usage_index == usage_index;
    };
    return match(0, 0, 0, kD3dDeclTypeFloat3, kD3dDeclUsagePosition, 0) &&
           match(1, 0, 12, kD3dDeclTypeD3dColor, kD3dDeclUsageNormal, 0) &&
           match(2, 0, 16, kD3dDeclTypeD3dColor, kD3dDeclUsageTangent, 0) &&
           match(3, 0, 20, kD3dDeclTypeD3dColor, kD3dDeclUsageBinormal, 0) &&
           match(4, 0, 24, kD3dDeclTypeD3dColor, kD3dDeclUsageColor, 0) &&
           match(5, 0, 28, kD3dDeclTypeShort2, kD3dDeclUsageTexcoord, 0) &&
           d[6].stream == 0xFF && d[6].offset == 0 &&
           d[6].type == kD3dDeclTypeUnused && d[6].method == 0 &&
           d[6].usage == 0 && d[6].usage_index == 0;
}

// F4.1: классификация фактических раскладок level.geom. Все поддержанные
// варианты имеют stride 32 и одинаковый префикс; различается только место
// TEXCOORD0, поэтому GPU-путь может обойтись двумя input layout.
VertexLayout12 ClassifyDeclaration(const VbInfo& vb) noexcept
{
    if (vb.stride != 32 || vb.declaration_count < 5 ||
        vb.declaration_count > 7)
        return VertexLayout12::Unsupported;

    const auto match = [&vb](uint32_t index, uint16_t offset, uint8_t type,
                             uint8_t usage, uint8_t usage_index) noexcept
    {
        const DeclarationElement& element = vb.declaration[index];
        return element.stream == 0 && element.offset == offset &&
               element.type == type && element.method == 0 &&
               element.usage == usage && element.usage_index == usage_index;
    };

    if (!match(0, 0, kD3dDeclTypeFloat3, kD3dDeclUsagePosition, 0) ||
        !match(1, 12, kD3dDeclTypeD3dColor, kD3dDeclUsageNormal, 0) ||
        !match(2, 16, kD3dDeclTypeD3dColor, kD3dDeclUsageTangent, 0) ||
        !match(3, 20, kD3dDeclTypeD3dColor, kD3dDeclUsageBinormal, 0))
        return VertexLayout12::Unsupported;

    if (vb.declaration_count == 7 &&
        match(4, 24, kD3dDeclTypeD3dColor, kD3dDeclUsageColor, 0) &&
        match(5, 28, kD3dDeclTypeShort2, kD3dDeclUsageTexcoord, 0))
        return VertexLayout12::Classic;

    if (match(4, 24, kD3dDeclTypeShort2, kD3dDeclUsageTexcoord, 0))
        return VertexLayout12::UvAt24;

    // SHORT4 в позиции TEXCOORD0 — это (u, v, lmu, lmv): первые две short
    // компоненты и есть базовая UV, поэтому раскладка совпадает с UvAt24.
    if (vb.declaration_count == 6 &&
        match(4, 24, kD3dDeclTypeShort4, kD3dDeclUsageTexcoord, 0))
        return VertexLayout12::UvAt24;

    return VertexLayout12::Unsupported;
}

ReadError ParseAndPublish(std::span<const uint8_t> level,
                          std::span<const uint8_t> geom,
                          uint32_t visual_id,
                          Geometry& output,
                          std::string& reason,
                          const Limits& limits)
{
    if (level.empty())
        return Fail(reason, ReadError::EmptyLevelInput);
    if (geom.empty())
        return Fail(reason, ReadError::EmptyGeomInput);
    if (level.size() > limits.maxLevelBytes)
        return Fail(reason, ReadError::LevelInputTooLarge);
    if (geom.size() > limits.maxGeomBytes)
        return Fail(reason, ReadError::GeomInputTooLarge);

    Chunk visual{};
    uint64_t visual_source_offset = 0;
    ReadError error = FindVisual(level, visual_id, visual, visual_source_offset, reason);
    if (error != ReadError::None)
        return error;

    uint32_t shader_id = 0;
    Bounds header_bounds{};
    error = ParseVisualHeader(level, visual, shader_id, header_bounds, reason);
    if (error != ReadError::None)
        return error;

    uint32_t vb_id = 0, vbase = 0, vcount = 0;
    uint32_t ib_id = 0, ibase = 0, icount = 0;
    error = ParseContainer(level, visual, vb_id, vbase, vcount,
                           ib_id, ibase, icount, reason);
    if (error != ReadError::None)
        return error;

    std::vector<VbInfo> vbs;
    std::vector<IbInfo> ibs;
    error = ParseGeometry(geom, vbs, ibs, reason);
    if (error != ReadError::None)
        return error;

    if (vb_id >= vbs.size())
        return Fail(reason, ReadError::InvalidSourceVbId);
    if (ib_id >= ibs.size())
        return Fail(reason, ReadError::InvalidSourceIbId);

    const VbInfo& vb = vbs[vb_id];
    const IbInfo& ib = ibs[ib_id];
    if (!IsSelectedDeclaration(vb))
        return Fail(reason, ReadError::UnsupportedDeclaration);
    if (vcount == 0 || vcount > kMaxVertexCount ||
        icount == 0 || icount > kMaxIndexCount || (icount % 3) != 0)
        return Fail(reason, ReadError::InvalidVisualCounts);
    if (vbase > vb.count || vcount > vb.count - vbase)
        return Fail(reason, ReadError::VertexRangeOutsideVb);
    if (ibase > ib.count || icount > ib.count - ibase)
        return Fail(reason, ReadError::IndexRangeOutsideIb);

    const uint64_t vertex_bytes = static_cast<uint64_t>(vcount) * vb.stride;
    const uint64_t index_bytes = static_cast<uint64_t>(icount) * 2ull;
    if (vertex_bytes > limits.maxOutputBytes ||
        index_bytes > limits.maxOutputBytes - vertex_bytes)
        return Fail(reason, ReadError::OutputBudgetExceeded);

    const uint64_t vertex_offset = vb.data_offset + static_cast<uint64_t>(vbase) * vb.stride;
    const uint64_t index_offset = ib.data_offset + static_cast<uint64_t>(ibase) * 2ull;
    if (vertex_offset > geom.size() ||
        vertex_bytes > geom.size() - static_cast<size_t>(vertex_offset))
        return Fail(reason, ReadError::VertexRangeOutsideGeom);
    if (index_offset > geom.size() ||
        index_bytes > geom.size() - static_cast<size_t>(index_offset))
        return Fail(reason, ReadError::IndexRangeOutsideGeom);

    Geometry temporary;
    temporary.visual_id = visual_id;
    temporary.shader_id = shader_id;
    temporary.source_visual_offset = visual_source_offset;
    temporary.source_visual_size = visual.size;
    temporary.source_vb_id = vb_id;
    temporary.source_vbase = vbase;
    temporary.source_vcount = vcount;
    temporary.source_ib_id = ib_id;
    temporary.source_ibase = ibase;
    temporary.source_icount = icount;
    temporary.source_vertex_byte_offset = vertex_offset;
    temporary.source_index_byte_offset = index_offset;
    temporary.first_index = 0;
    temporary.base_vertex = 0;
    temporary.index_count = icount;
    temporary.triangle_count = icount / 3;
    temporary.vertex_stride = vb.stride;
    temporary.vertex_count = vcount;
    temporary.declaration_count = vb.declaration_count;
    temporary.declaration = vb.declaration;
    temporary.header_bounds = header_bounds;
    temporary.vertex_bytes.assign(
        geom.begin() + static_cast<size_t>(vertex_offset),
        geom.begin() + static_cast<size_t>(vertex_offset + vertex_bytes));
    temporary.indices.resize(icount);

    bool finite = true;
    bool have_position = false;
    float minimum[3] = {std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max()};
    float maximum[3] = {-std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max()};
    for (uint32_t i = 0; i < vcount; ++i)
    {
        const uint64_t base = vertex_offset + static_cast<uint64_t>(i) * vb.stride;
        float p[3] = {0.0f, 0.0f, 0.0f};
        for (int c = 0; c < 3; ++c)
        {
            if (!ReadF32(geom, base + c * 4, p[c]))
                return Fail(reason, ReadError::TruncatedPosition);
            if (!std::isfinite(p[c]))
                finite = false;
            minimum[c] = std::min(minimum[c], p[c]);
            maximum[c] = std::max(maximum[c], p[c]);
        }
        have_position = true;
    }
    if (!finite)
        return Fail(reason, ReadError::NonFinitePosition);
    if (!have_position)
        return Fail(reason, ReadError::InvalidVisualCounts);

    for (int c = 0; c < 3; ++c)
    {
        temporary.bounds.minimum[c] = minimum[c];
        temporary.bounds.maximum[c] = maximum[c];
        if (minimum[c] < header_bounds.minimum[c] - kHeaderBoundsTolerance ||
            maximum[c] > header_bounds.maximum[c] + kHeaderBoundsTolerance)
            return Fail(reason, ReadError::ComputedBoundsOutsideHeader);
    }

    // Сначала окончательно фиксируем центр во float. Затем расстояния
    // считаются в double уже от опубликованного float-центра, а не от
    // неокруглённого double-центра.
    double center_d[3] = {
        (static_cast<double>(minimum[0]) + maximum[0]) * 0.5,
        (static_cast<double>(minimum[1]) + maximum[1]) * 0.5,
        (static_cast<double>(minimum[2]) + maximum[2]) * 0.5};
    float center_f[3] = {0.0f, 0.0f, 0.0f};
    for (int c = 0; c < 3; ++c)
    {
        if (!std::isfinite(center_d[c]) ||
            center_d[c] < -static_cast<double>(std::numeric_limits<float>::max()) ||
            center_d[c] > static_cast<double>(std::numeric_limits<float>::max()))
            return Fail(reason, ReadError::InvalidComputedSphere);
        center_f[c] = static_cast<float>(center_d[c]);
        if (!std::isfinite(center_f[c]))
            return Fail(reason, ReadError::InvalidComputedSphere);
    }

    double max_distance = 0.0;
    double distance_scale = 1.0;
    for (uint32_t i = 0; i < vcount; ++i)
    {
        const uint64_t base = vertex_offset + static_cast<uint64_t>(i) * vb.stride;
        float p[3] = {0.0f, 0.0f, 0.0f};
        for (int c = 0; c < 3; ++c)
        {
            if (!ReadF32(geom, base + c * 4, p[c]))
                return Fail(reason, ReadError::TruncatedPosition);
        }
        const double dx = static_cast<double>(p[0]) - static_cast<double>(center_f[0]);
        const double dy = static_cast<double>(p[1]) - static_cast<double>(center_f[1]);
        const double dz = static_cast<double>(p[2]) - static_cast<double>(center_f[2]);
        const double distance = std::hypot(dx, dy, dz);
        if (!std::isfinite(distance))
            return Fail(reason, ReadError::InvalidComputedSphere);
        max_distance = std::max(max_distance, distance);
        distance_scale = std::max(distance_scale,
                                  std::max(std::abs(dx),
                                           std::max(std::abs(dy), std::abs(dz))));
    }

    // Консервативное округление вверх: малый margin на double-погрешность
    // и обязательный float nextafter. Произвольный огромный множитель не
    // используется.
    const double margin = 16.0 * std::numeric_limits<double>::epsilon() * distance_scale;
    const double candidate = max_distance + margin;
    if (!std::isfinite(candidate))
        return Fail(reason, ReadError::InvalidComputedSphere);
    float radius = static_cast<float>(candidate);
    if (!std::isfinite(radius))
        return Fail(reason, ReadError::InvalidComputedSphere);
    radius = std::nextafter(radius, std::numeric_limits<float>::infinity());
    if (!std::isfinite(radius))
        return Fail(reason, ReadError::InvalidComputedSphere);

    // Финальная проверка покрытия использует уже опубликованные float center
    // и radius. Именно на этот контракт будет опираться будущий culling.
    for (uint32_t i = 0; i < vcount; ++i)
    {
        const uint64_t base = vertex_offset + static_cast<uint64_t>(i) * vb.stride;
        float p[3] = {0.0f, 0.0f, 0.0f};
        for (int c = 0; c < 3; ++c)
        {
            if (!ReadF32(geom, base + c * 4, p[c]))
                return Fail(reason, ReadError::TruncatedPosition);
        }
        const double dx = static_cast<double>(p[0]) - static_cast<double>(center_f[0]);
        const double dy = static_cast<double>(p[1]) - static_cast<double>(center_f[1]);
        const double dz = static_cast<double>(p[2]) - static_cast<double>(center_f[2]);
        const double distance = std::hypot(dx, dy, dz);
        if (!std::isfinite(distance) || distance > static_cast<double>(radius))
            return Fail(reason, ReadError::InvalidComputedSphere);
    }

    temporary.sphere.center[0] = center_f[0];
    temporary.sphere.center[1] = center_f[1];
    temporary.sphere.center[2] = center_f[2];
    temporary.sphere.radius = radius;

    for (uint32_t i = 0; i < icount; ++i)
    {
        uint16_t index = 0;
        if (!ReadU16(geom, index_offset + static_cast<uint64_t>(i) * 2, index))
            return Fail(reason, ReadError::TruncatedIndex);
        if (index >= vcount)
            return Fail(reason, ReadError::IndexOutsideVertexRange);
        temporary.indices[i] = index;
    }

    output = std::move(temporary);
    SetReasonBestEffort(reason, ReadError::None);
    return ReadError::None;
}
} // namespace

std::string_view FindShaderBinding(std::span<const uint8_t> level,
                                   uint32_t shaderId) noexcept
{
    if (shaderId == 0)
        return {};
    Chunk shaders;
    std::string reason;
    if (FindUniqueChunk(level, kLevelShadersChunk, shaders, reason) != ReadError::None ||
        shaders.size < sizeof(uint32_t))
        return {};
    uint32_t count = 0;
    if (!ReadU32(level, shaders.payload, count) || shaderId >= count ||
        count > shaders.size - sizeof(uint32_t))
        return {};

    const char* current = reinterpret_cast<const char*>(level.data() + shaders.payload + 4);
    const char* end = reinterpret_cast<const char*>(level.data() + shaders.payload + shaders.size);
    for (uint32_t index = 0; index <= shaderId; ++index)
    {
        const char* terminator = std::find(current, end, '\0');
        if (terminator == end)
            break;
        if (index == shaderId)
            return {current, static_cast<size_t>(terminator - current)};
        current = terminator + 1;
    }
    return {};
}

void Geometry::clear() noexcept
{
    *this = Geometry{};
}

ReadError ReadVisual(std::span<const uint8_t> level_bytes,
                     std::span<const uint8_t> geom_bytes,
                     uint32_t visual_id,
                     Geometry& output,
                     std::string& reason,
                     const Limits& limits) noexcept
{
    // Проверяем aliasing до очистки output/reason: обе операции могут
    // освободить память, на которую указывает input span.
    if (InputsAliasOutput(level_bytes, geom_bytes, output, reason))
    {
        output.clear();
        SetReasonBestEffort(reason, ReadError::InputOutputAliasing);
        return ReadError::InputOutputAliasing;
    }

    try
    {
        output.clear();
        SetReasonBestEffort(reason, ReadError::None);
        return ParseAndPublish(level_bytes, geom_bytes, visual_id, output, reason, limits);
    }
    catch (const std::bad_alloc&)
    {
        output.clear();
        SetReasonBestEffort(reason, ReadError::AllocationFailure);
        return ReadError::AllocationFailure;
    }
    catch (const std::exception&)
    {
        output.clear();
        SetReasonBestEffort(reason, ReadError::UnexpectedException);
        return ReadError::UnexpectedException;
    }
    catch (...)
    {
        output.clear();
        SetReasonBestEffort(reason, ReadError::UnexpectedException);
        return ReadError::UnexpectedException;
    }
}

namespace
{
// Границы visual уже лежат в OGF_HEADER в мировых координатах (level.geom
// запечён level-компилятором), поэтому отдельный проход по вершинам для
// culling-сферы не нужен.
void MakeSphere(const Bounds& bounds, float center[3], float& radius) noexcept
{
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    for (int c = 0; c < 3; ++c)
    {
        center[c] = static_cast<float>(
            (static_cast<double>(bounds.minimum[c]) +
             static_cast<double>(bounds.maximum[c])) * 0.5);
        const double half = (static_cast<double>(bounds.maximum[c]) -
                             static_cast<double>(bounds.minimum[c])) * 0.5;
        if (c == 0)
            dx = half;
        else if (c == 1)
            dy = half;
        else
            dz = half;
    }
    radius = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    if (!std::isfinite(radius) || radius < 0.0f)
        radius = 0.0f;
}

// Разбирает один visual верхнего контейнера. Возвращает false, если visual
// нельзя описать диапазоном пулов; причина остаётся в счётчике reject.
bool ReadPoolVisual(std::span<const uint8_t> level, const Chunk& chunk,
                    const std::vector<std::vector<SwiWindow>>* swiTables,
                    PoolVisual& visual, uint32_t& rejectChunk)
{
    const uint64_t end = chunk.payload + chunk.size;
    uint64_t pos = chunk.payload;
    rejectChunk = 0;
    bool has_header = false;
    bool has_gc = false;
    bool has_swi = false;
    bool has_swi_container = false;
    uint32_t swi_container_id = 0;
    bool has_tree = false;
    float tree_world[16] = {};
    float tree_color[10] = {};
    uint32_t swi_offset = 0;
    uint16_t swi_num_tris = 0;
    uint16_t swi_num_verts = 0;
    uint32_t vb_id = 0;
    uint32_t vbase = 0;
    uint32_t vcount = 0;
    uint32_t ib_id = 0;
    uint32_t ibase = 0;
    uint32_t icount = 0;

    while (pos < end)
    {
        uint32_t type = 0;
        uint32_t size = 0;
        if (end - pos < 8)
            return false;
        if (!ReadU32(level, pos, type) || !ReadU32(level, pos + 4, size))
            return false;
        if ((type & kCompressMark) != 0)
            return false;
        const uint64_t data = pos + 8;
        if (size > end - data)
            return false;

        if (type == kOgfHeader)
        {
            if (has_header || size != 44)
                return false;
            if (level[static_cast<size_t>(data)] != kOgfVersion)
                return false;
            visual.type = level[static_cast<size_t>(data + 1)];
            uint16_t shader_id = 0;
            if (!ReadU16(level, data + 2, shader_id))
                return false;
            visual.shader_id = shader_id;
            for (int i = 0; i < 3; ++i)
            {
                if (!ReadF32(level, data + 4 + i * 4,
                             visual.header_bounds.minimum[i]) ||
                    !ReadF32(level, data + 16 + i * 4,
                             visual.header_bounds.maximum[i]))
                    return false;
                if (!std::isfinite(visual.header_bounds.minimum[i]) ||
                    !std::isfinite(visual.header_bounds.maximum[i]))
                    return false;
            }
            if (visual.header_bounds.minimum[0] >
                    visual.header_bounds.maximum[0] ||
                visual.header_bounds.minimum[1] >
                    visual.header_bounds.maximum[1] ||
                visual.header_bounds.minimum[2] >
                    visual.header_bounds.maximum[2])
                return false;
            has_header = true;
        }
        else if (type == kOgfTexture)
        {
            // FBasicVisual::Load переопределяет shader_id парой OGF_TEXTURE.
            rejectChunk = type;
            if (!visual.shader_override.empty())
                return false;
            constexpr size_t maxNameBytes = 256;
            const char* begin = reinterpret_cast<const char*>(level.data() + data);
            const char* textureEnd = begin + (std::min)(size_t{size}, maxNameBytes);
            const char* textureZero = std::find(begin, textureEnd, '\0');
            if (textureZero == textureEnd)
                return false;
            const char* shader = textureZero + 1;
            const size_t remaining = size - static_cast<size_t>(shader - begin);
            const char* shaderEnd = shader + (std::min)(remaining, maxNameBytes);
            const char* shaderZero = std::find(shader, shaderEnd, '\0');
            if (shaderZero == shader || shaderZero == shaderEnd)
                return false;
            visual.texture_override.assign(begin, textureZero);
            visual.shader_override.assign(shader, shaderZero);
            rejectChunk = 0;
        }
        else if (type == kOgfGcContainer)
        {
            if (has_gc || size != 24)
                return false;
            if (!ReadU32(level, data + 0, vb_id) ||
                !ReadU32(level, data + 4, vbase) ||
                !ReadU32(level, data + 8, vcount) ||
                !ReadU32(level, data + 12, ib_id) ||
                !ReadU32(level, data + 16, ibase) ||
                !ReadU32(level, data + 20, icount))
                return false;
            has_gc = true;
        }
        else if (type == kOgfFastPath)
        {
            // Payload fast-path не используется ни R4, ни этим reader-ом.
        }
        else if (type == kOgfSwiData)
        {
            // 4 reserved u32, count, затем count записей FSlideWindow.
            if (has_swi || size < kSwiHeaderBytes)
                return false;
            uint32_t swiCount = 0;
            if (!ReadU32(level, data + 16, swiCount) || swiCount == 0 ||
                swiCount > 4096)
                return false;
            if (size < kSwiHeaderBytes +
                           static_cast<uint64_t>(swiCount) * kSlideWindowBytes)
                return false;
            // R4 при максимальном LOD берёт окно 0 — это самая подробная
            // геометрия. Для статической сцены этого достаточно; выбор LOD по
            // расстоянию относится к следующим этапам.
            if (!ReadU32(level, data + kSwiHeaderBytes, swi_offset) ||
                !ReadU16(level, data + kSwiHeaderBytes + 4, swi_num_tris) ||
                !ReadU16(level, data + kSwiHeaderBytes + 6, swi_num_verts))
                return false;
            has_swi = true;
        }
        else if (type == kOgfSwiContainer)
        {
            // OGF_SWICONTAINER: u32 id записи в таблице SWI уровня.
            if (has_swi_container || size < 4)
                return false;
            if (!ReadU32(level, data, swi_container_id))
                return false;
            has_swi_container = true;
        }
        else if (type == kOgfTreeDef2)
        {
            // Fmatrix (16 float) + два блока по 5 float (color/hemi).
            if (has_tree || size < kTreeDefBytes)
                return false;
            for (uint32_t element = 0; element < 16; ++element)
            {
                if (!ReadF32(level, data + element * 4, tree_world[element]))
                    return false;
                if (!std::isfinite(tree_world[element]))
                    return false;
            }
            for (uint32_t element = 0; element < 10; ++element)
            {
                if (!ReadF32(level, data + (16 + element) * 4, tree_color[element]) ||
                    !std::isfinite(tree_color[element]))
                    return false;
            }
            has_tree = true;
        }
        else
        {
            rejectChunk = type;
            return false;
        }
        pos = data + size;
    }

    if (!has_header || !has_gc)
        return false;
    visual.vb_id = vb_id;
    visual.vbase = vbase;
    visual.vcount = vcount;
    visual.ib_id = ib_id;
    visual.ibase = ibase;
    visual.icount = icount;
    const bool isTree = visual.type == kMtTreeSt || visual.type == kMtTreePm;
    // MT_TREE_PM: окно берётся из таблицы SWI уровня по id из chunk 20,
    // как FTreeVisual_PM::Load -> RenderInstanced (SW.num_verts,
    // iBase + SW.offset, SW.num_tris).
    if (visual.type == kMtTreePm)
    {
        if (!has_swi_container || swiTables == nullptr ||
            swi_container_id >= swiTables->size())
            return false;
        const auto& windows = (*swiTables)[swi_container_id];
        if (windows.empty())
            return false;
        const SwiWindow& window = windows.front();
        if (window.num_tris == 0 || window.num_verts == 0)
            return false;
        visual.ibase = ibase + window.offset;
        visual.icount = static_cast<uint32_t>(window.num_tris) * 3u;
        visual.vcount = window.num_verts;
        visual.swi_id = swi_container_id;
        visual.has_swi = true;
    }
    else if (visual.type == kMtProgressive || (isTree && has_swi))
    {
        if (!has_swi || swi_num_tris == 0 || swi_num_verts == 0)
            return false;
        visual.ibase = ibase + swi_offset;
        visual.icount = static_cast<uint32_t>(swi_num_tris) * 3u;
        visual.vcount = swi_num_verts;
    }
    if (isTree)
    {
        if (!has_tree)
            return false;
        // FTreeVisual::Load: hemi каждого блока умножается на 0.5 * 1.5 * 1.3333.
        constexpr float r4HemiFactor = 0.5f * 1.5f * 1.3333f;
        visual.tree_hemi_scale = tree_color[3] * r4HemiFactor;
        visual.tree_hemi_bias = tree_color[8] * r4HemiFactor;
        if (!std::isfinite(visual.tree_hemi_scale) || !std::isfinite(visual.tree_hemi_bias))
            return false;
        visual.has_world = true;
        for (uint32_t element = 0; element < 16; ++element)
            visual.world[element] = tree_world[element];
    }
    return true;
}
} // namespace

const char* VertexLayoutName(VertexLayout12 layout) noexcept
{
    switch (layout)
    {
    case VertexLayout12::Classic:
        return "pos/normal/tangent/binormal/color@24/uv@28";
    case VertexLayout12::UvAt24:
        return "pos/normal/tangent/binormal/uv@24";
    default:
        return "unsupported";
    }
}

ReadError ReadLevelPools(std::span<const uint8_t> level_bytes,
                         std::span<const uint8_t> geom_bytes,
                         LevelPools& output, std::string& reason,
                         const Limits& limits) noexcept
{
    output = LevelPools{};
    SetReasonBestEffort(reason, ReadError::None);
    if (level_bytes.empty())
        return Fail(reason, ReadError::EmptyLevelInput);
    if (geom_bytes.empty())
        return Fail(reason, ReadError::EmptyGeomInput);
    if (level_bytes.size() > limits.maxLevelBytes)
        return Fail(reason, ReadError::LevelInputTooLarge);
    if (geom_bytes.size() > limits.maxGeomBytes)
        return Fail(reason, ReadError::GeomInputTooLarge);

    try
    {
        Chunk visuals{};
        ReadError error =
            FindUniqueChunk(level_bytes, kLevelVisualsChunk, visuals, reason);
        if (error != ReadError::None)
            return error;

        std::vector<VbInfo> vbs;
        std::vector<IbInfo> ibs;
        error = ParseGeometry(geom_bytes, vbs, ibs, reason);
        if (error != ReadError::None)
            return error;

        LevelPools candidate;
        // F4.1-E: таблица SWI уровня. R4 читает её из level.geom
        // (r4_loader.cpp: LoadSWIs(geom)), а не из level: item_count, затем на
        // запись 4 reserved u32, u32 count, count * FSlideWindow.
        {
            Chunk swisChunk{};
            const ReadError swisError =
                FindUniqueChunk(geom_bytes, kLevelSwisChunk, swisChunk, reason);
            if (swisError == ReadError::None)
            {
                uint64_t swiPos = swisChunk.payload;
                const uint64_t swiEnd = swisChunk.payload + swisChunk.size;
                uint32_t itemCount = 0;
                if (swiEnd - swiPos < 4 ||
                    !ReadU32(geom_bytes, swiPos, itemCount) ||
                    itemCount > kMaxSwiItems)
                    return Fail(reason, ReadError::TruncatedChunkHeader);
                swiPos += 4;
                candidate.swi_tables.resize(itemCount);
                for (uint32_t item = 0; item < itemCount; ++item)
                {
                    if (swiEnd - swiPos < kSwiHeaderBytes)
                        return Fail(reason, ReadError::TruncatedChunkHeader);
                    uint32_t windowCount = 0;
                    if (!ReadU32(geom_bytes, swiPos + 16, windowCount) ||
                        windowCount > kMaxSwiWindows)
                        return Fail(reason, ReadError::TruncatedChunkHeader);
                    swiPos += kSwiHeaderBytes;
                    const uint64_t bytes =
                        static_cast<uint64_t>(windowCount) * kSlideWindowBytes;
                    if (bytes > swiEnd - swiPos)
                        return Fail(reason, ReadError::ChunkExceedsInput);
                    auto& windows = candidate.swi_tables[item];
                    windows.resize(windowCount);
                    for (uint32_t window = 0; window < windowCount; ++window)
                    {
                        SwiWindow entry;
                        if (!ReadU32(geom_bytes, swiPos, entry.offset) ||
                            !ReadU16(geom_bytes, swiPos + 4, entry.num_tris) ||
                            !ReadU16(geom_bytes, swiPos + 6, entry.num_verts))
                            return Fail(reason, ReadError::TruncatedChunkHeader);
                        windows[window] = entry;
                        swiPos += kSlideWindowBytes;
                    }
                }
            }
            else if (swisError != ReadError::MissingMandatoryChunk)
            {
                // Отсутствие таблицы не фатально: level без PM-деревьев её не
                // содержит. Любая другая ошибка — порча контейнера.
                return swisError;
            }
        }

        candidate.vertex_pools.reserve(vbs.size());
        for (const VbInfo& vb : vbs)
        {
            VertexPoolInfo info;
            info.id = vb.id;
            info.count = vb.count;
            info.stride = vb.stride;
            info.data_offset = vb.data_offset;
            info.declaration_count = vb.declaration_count;
            info.declaration = vb.declaration;
            info.layout = ClassifyDeclaration(vb);
            info.supported = info.layout != VertexLayout12::Unsupported;
            candidate.vertex_payload_bytes +=
                static_cast<uint64_t>(vb.count) * vb.stride;
            candidate.vertex_pools.push_back(info);
        }
        candidate.index_pools.reserve(ibs.size());
        for (const IbInfo& ib : ibs)
        {
            IndexPoolInfo info;
            info.id = ib.id;
            info.count = ib.count;
            info.data_offset = ib.data_offset;
            candidate.index_pools.push_back(info);
        }

        // F4.1-E: гистограмма отвергнутых подчанков по типу визуала. Без неё
        // причина отказа «тип» маскирует реальную (например неизвестный
        // OGF-подчанок у MT_TREE_PM).
        uint32_t rejectChunks[16][32] = {};

        const uint64_t end = visuals.payload + visuals.size;
        uint64_t pos = visuals.payload;
        while (pos < end)
        {
            uint32_t type = 0;
            uint32_t size = 0;
            if (end - pos < 8)
                return Fail(reason, ReadError::TruncatedVisualChunkHeader);
            if (!ReadU32(level_bytes, pos, type) ||
                !ReadU32(level_bytes, pos + 4, size))
                return Fail(reason, ReadError::TruncatedVisualChunkHeader);
            if ((type & kCompressMark) != 0)
                return Fail(reason, ReadError::CompressedChunk);
            const uint64_t payload = pos + 8;
            if (size > end - payload)
                return Fail(reason, ReadError::VisualChunkExceedsContainer);

            Chunk chunk{};
            chunk.id = type;
            chunk.payload = payload;
            chunk.size = size;

            PoolVisual visual;
            visual.id = type;
            uint32_t rejectChunk = 0;
            if (!ReadPoolVisual(level_bytes, chunk, &candidate.swi_tables, visual,
                                rejectChunk))
            {
                visual.drawable = false;
                visual.reject = visual.type == kMtNormal
                                    ? ReadError::UnsupportedOgfSubchunk
                                    : ReadError::UnsupportedVisualType;
                if (rejectChunk != 0 && visual.type < 16 && rejectChunk < 32)
                    ++rejectChunks[visual.type][rejectChunk];
            }
            else if (visual.type != kMtNormal && visual.type != kMtProgressive &&
                     visual.type != kMtTreeSt && visual.type != kMtTreePm)
            {
                visual.drawable = false;
                visual.reject = ReadError::UnsupportedVisualType;
            }
            else if (visual.vb_id >= candidate.vertex_pools.size())
            {
                visual.drawable = false;
                visual.reject = ReadError::InvalidSourceVbId;
            }
            else if (visual.ib_id >= candidate.index_pools.size())
            {
                visual.drawable = false;
                visual.reject = ReadError::InvalidSourceIbId;
            }
            else if (!candidate.vertex_pools[visual.vb_id].supported)
            {
                visual.drawable = false;
                visual.reject = ReadError::UnsupportedDeclaration;
            }
            else
            {
                const VertexPoolInfo& vb = candidate.vertex_pools[visual.vb_id];
                const IndexPoolInfo& ib = candidate.index_pools[visual.ib_id];
                if (visual.vcount == 0 || visual.icount == 0 ||
                    (visual.icount % 3) != 0)
                {
                    visual.drawable = false;
                    visual.reject = ReadError::InvalidVisualCounts;
                }
                else if (visual.vbase > vb.count ||
                         visual.vcount > vb.count - visual.vbase ||
                         visual.ibase > ib.count ||
                         visual.icount > ib.count - visual.ibase)
                {
                    visual.drawable = false;
                    visual.reject = ReadError::VertexRangeOutsideVb;
                }
                else
                {
                    visual.drawable = true;
                    visual.layout = candidate.vertex_pools[visual.vb_id].layout;
                    float center[3] = {0.0f, 0.0f, 0.0f};
                    float radius = 0.0f;
                    MakeSphere(visual.header_bounds, center, radius);
                    for (int c = 0; c < 3; ++c)
                        visual.sphere_center[c] = center[c];
                    visual.sphere_radius = radius;
                }
            }

            if (visual.drawable)
                ++candidate.visuals_drawable;
            else
                ++candidate.visuals_rejected;
            candidate.visuals.push_back(std::move(visual));
            pos = payload + size;
        }

        output = std::move(candidate);
        // Диагностика печатается владельцем через LogDeclarationInventory;
        // сама гистограмма живёт в LevelPools::rejected_chunks.
        for (uint32_t type = 0; type < 16; ++type)
        {
            for (uint32_t chunkId = 0; chunkId < 32; ++chunkId)
            {
                if (rejectChunks[type][chunkId] != 0)
                    output.rejected_chunks.push_back(
                        {type, chunkId, rejectChunks[type][chunkId]});
            }
        }
        return ReadError::None;
    }
    catch (const std::bad_alloc&)
    {
        output = LevelPools{};
        return Fail(reason, ReadError::AllocationFailure);
    }
    catch (const std::exception&)
    {
        output = LevelPools{};
        return Fail(reason, ReadError::UnexpectedException);
    }
    catch (...)
    {
        output = LevelPools{};
        return Fail(reason, ReadError::UnexpectedException);
    }
}
} // namespace xraylevel12
