// F4.0-B1: изолированный CPU-only test harness.
//
// Исполняемый файл намеренно не зависит от game executable, R4, DX11, DX12,
// CHW12 и VFS. CPU reader компилируется как обычный C++; реальные байты
// передаются только если вызывающий код указал локальные пути.

#include "dx12xraylevelgeometry.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifdef XRAYLEVEL_OOM_TEST
namespace
{
std::atomic<bool> g_fail_next_allocation{false};

void* AllocateOrThrow(std::size_t size)
{
    if (g_fail_next_allocation.exchange(false, std::memory_order_acq_rel))
        throw std::bad_alloc();
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new(std::size_t size)
{
    return AllocateOrThrow(size);
}

void* operator new[](std::size_t size)
{
    return AllocateOrThrow(size);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
#endif

namespace
{
using Bytes = std::vector<uint8_t>;

constexpr uint32_t kVisualId = 3;
constexpr uint32_t kShaderId = 7;
constexpr uint32_t kDeclarationElements = 7; // шесть атрибутов + terminator
constexpr uint32_t kD3dDeclTypeUnused = 17;
constexpr uint32_t kCompressMark = 0x80000000u;

void AppendU16(Bytes& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void AppendU32(Bytes& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void AppendF32(Bytes& out, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(out, bits);
}

void WriteU16(Bytes& out, size_t offset, uint16_t value)
{
    out[offset] = static_cast<uint8_t>(value & 0xff);
    out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void WriteU32(Bytes& out, size_t offset, uint32_t value)
{
    out[offset] = static_cast<uint8_t>(value & 0xff);
    out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
    out[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

void WriteF32(Bytes& out, size_t offset, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteU32(out, offset, bits);
}

uint32_t ReadU32At(const Bytes& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

void SetChunkCompressed(Bytes& bytes, size_t chunk_header)
{
    if (chunk_header != std::string::npos)
        WriteU32(bytes, chunk_header, ReadU32At(bytes, chunk_header) | kCompressMark);
}

void AppendChunk(Bytes& out, uint32_t type, const Bytes& payload)
{
    AppendU32(out, type);
    AppendU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

void AppendDeclElement(Bytes& out, uint16_t stream, uint16_t offset,
                       uint8_t type, uint8_t usage, uint8_t usage_index)
{
    AppendU16(out, stream);
    AppendU16(out, offset);
    out.push_back(type);
    out.push_back(0); // метод D3DDECLMETHOD_DEFAULT
    out.push_back(usage);
    out.push_back(usage_index);
}

Bytes SelectedDeclaration()
{
    Bytes declaration;
    AppendDeclElement(declaration, 0, 0, 2, 0, 0);   // POSITION FLOAT3
    AppendDeclElement(declaration, 0, 12, 4, 3, 0);  // NORMAL D3DCOLOR
    AppendDeclElement(declaration, 0, 16, 4, 6, 0);  // TANGENT D3DCOLOR
    AppendDeclElement(declaration, 0, 20, 4, 7, 0);  // BINORMAL D3DCOLOR
    AppendDeclElement(declaration, 0, 24, 4, 10, 0); // COLOR D3DCOLOR
    AppendDeclElement(declaration, 0, 28, 6, 5, 0);  // TEXCOORD SHORT2
    AppendDeclElement(declaration, 0xFF, 0, 17, 0, 0); // канонический D3DDECL_END
    return declaration;
}

Bytes VertexRow(float x, float y, float z)
{
    Bytes row(32, 0);
    WriteF32(row, 0, x);
    WriteF32(row, 4, y);
    WriteF32(row, 8, z);
    for (size_t i = 12; i < row.size(); ++i)
        row[i] = static_cast<uint8_t>(0x80 + i);
    return row;
}

size_t FindChunkInRange(const Bytes& bytes, size_t start, size_t end, uint32_t wanted)
{
    if (start > end || end > bytes.size())
        return std::string::npos;
    size_t pos = start;
    while (pos + 8 <= end)
    {
        const uint32_t type = ReadU32At(bytes, pos);
        const uint32_t size = ReadU32At(bytes, pos + 4);
        if ((type & 0x7fffffffu) == wanted)
            return pos + 8;
        if (size > end - (pos + 8))
            return std::string::npos;
        pos += 8 + size;
    }
    return std::string::npos;
}

size_t FindChunk(const Bytes& bytes, uint32_t wanted)
{
    return FindChunkInRange(bytes, 0, bytes.size(), wanted);
}

struct Fixture
{
    Bytes level;
    Bytes geom;

    size_t level_visuals_header = 0;
    size_t visual_chunk_header = 0;
    size_t visual_payload = 0;
    size_t visual_size = 0;
    size_t header_header = 0;
    size_t fastpath_header = 0;
    size_t container_header = 0;
    size_t header = 0;
    size_t container = 0;
    size_t vb_header = 0;
    size_t ib_header = 0;
    size_t vb_chunk = 0;
    size_t ib_chunk = 0;
    size_t vb_declaration = 0;
    size_t vb_count = 0;
    size_t vb_data = 0;
    size_t ib_count = 0;
    size_t ib_data = 0;
};

Fixture BuildFixture(uint32_t vbase = 0, uint32_t vcount = 4,
                     uint32_t ibase = 0, uint32_t icount = 6,
                     uint32_t total_vertices = 4, uint32_t total_indices = 6,
                     bool include_container = true,
                     bool include_fastpath = true)
{
    Fixture f;
    if (total_vertices < vbase + vcount)
        total_vertices = vbase + vcount;
    if (total_indices < ibase + icount)
        total_indices = ibase + icount;

    Bytes vertices;
    float min_x = std::numeric_limits<float>::max();
    float max_x = -std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_y = -std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < total_vertices; ++i)
    {
        const float x = static_cast<float>(i);
        const float y = static_cast<float>(i % 3);
        const Bytes row = VertexRow(x, y, 0.0f);
        vertices.insert(vertices.end(), row.begin(), row.end());
        if (i >= vbase && i < vbase + vcount)
        {
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
    }

    Bytes indices;
    indices.resize(static_cast<size_t>(total_indices) * 2, 0);
    const uint16_t pattern[6] = {0, 1, 2, 2, 1, 3};
    for (uint32_t i = 0; i < icount && i < 6; ++i)
        WriteU16(indices, static_cast<size_t>(ibase + i) * 2, pattern[i]);

    const float margin = 0.5f;
    Bytes header_payload(44, 0);
    header_payload[0] = 4; // версия OGF
    header_payload[1] = 0; // MT_NORMAL
    WriteU16(header_payload, 2, static_cast<uint16_t>(kShaderId));
    WriteF32(header_payload, 4, min_x - margin);
    WriteF32(header_payload, 8, min_y - margin);
    WriteF32(header_payload, 12, -margin);
    WriteF32(header_payload, 16, max_x + margin);
    WriteF32(header_payload, 20, max_y + margin);
    WriteF32(header_payload, 24, margin);
    // Заведомо бесполезная header sphere: reader обязан сам рассчитать
    // консервативную сферу по позициям.
    WriteF32(header_payload, 28, 0.0f);
    WriteF32(header_payload, 32, 0.0f);
    WriteF32(header_payload, 36, 0.0f);
    WriteF32(header_payload, 40, 0.0f);

    Bytes container_payload(24, 0);
    WriteU32(container_payload, 0, 0); // id VB
    WriteU32(container_payload, 4, vbase);
    WriteU32(container_payload, 8, vcount);
    WriteU32(container_payload, 12, 0); // id IB
    WriteU32(container_payload, 16, ibase);
    WriteU32(container_payload, 20, icount);

    Bytes visual_data;
    if (include_container)
        AppendChunk(visual_data, 21, container_payload);
    if (include_fastpath)
        AppendChunk(visual_data, 22, Bytes(32, 0));
    AppendChunk(visual_data, 1, header_payload);

    Bytes one_visual;
    AppendChunk(one_visual, kVisualId, visual_data);
    AppendChunk(f.level, 3, one_visual);

    Bytes declaration = SelectedDeclaration();
    Bytes vb_payload;
    AppendU32(vb_payload, 1);
    vb_payload.insert(vb_payload.end(), declaration.begin(), declaration.end());
    AppendU32(vb_payload, total_vertices);
    vb_payload.insert(vb_payload.end(), vertices.begin(), vertices.end());
    AppendChunk(f.geom, 9, vb_payload);

    Bytes ib_payload;
    AppendU32(ib_payload, 1);
    AppendU32(ib_payload, total_indices);
    ib_payload.insert(ib_payload.end(), indices.begin(), indices.end());
    AppendChunk(f.geom, 10, ib_payload);

    const size_t visuals = FindChunk(f.level, 3);
    const size_t visual_chunk_payload = visuals + 8;
    f.level_visuals_header = 0;
    f.visual_chunk_header = visuals;
    f.visual_payload = visual_chunk_payload;
    f.visual_size = visual_data.size();
    f.header = FindChunkInRange(f.level, visual_chunk_payload,
                                visual_chunk_payload + f.visual_size, 1);
    f.container = FindChunkInRange(f.level, visual_chunk_payload,
                                   visual_chunk_payload + f.visual_size, 21);
    f.fastpath_header = FindChunkInRange(f.level, visual_chunk_payload,
                                        visual_chunk_payload + f.visual_size, 22);
    f.header_header = f.header - 8;
    f.container_header = f.container - 8;
    if (f.fastpath_header != std::string::npos)
        f.fastpath_header -= 8;

    f.vb_chunk = FindChunk(f.geom, 9);
    f.ib_chunk = FindChunk(f.geom, 10);
    f.vb_header = f.vb_chunk - 8;
    f.ib_header = f.ib_chunk - 8;
    f.vb_declaration = f.vb_chunk + 4;
    f.vb_count = f.vb_declaration + declaration.size();
    f.vb_data = f.vb_count + 4;
    f.ib_count = f.ib_chunk + 4;
    f.ib_data = f.ib_count + 4;
    return f;
}

void SetHeaderBounds(Bytes& level, const Fixture& f,
                     float min_x, float min_y, float min_z,
                     float max_x, float max_y, float max_z)
{
    WriteF32(level, f.header + 4, min_x);
    WriteF32(level, f.header + 8, min_y);
    WriteF32(level, f.header + 12, min_z);
    WriteF32(level, f.header + 16, max_x);
    WriteF32(level, f.header + 20, max_y);
    WriteF32(level, f.header + 24, max_z);
}

void SetVertexPosition(Bytes& geom, const Fixture& f, uint32_t index,
                       float x, float y, float z)
{
    const size_t base = f.vb_data + static_cast<size_t>(index) * 32;
    WriteF32(geom, base + 0, x);
    WriteF32(geom, base + 4, y);
    WriteF32(geom, base + 8, z);
}

void GetVertexPosition(const xraylevel12::Geometry& g, uint32_t index, float (&p)[3])
{
    const uint8_t* row = g.vertex_bytes.data() + static_cast<size_t>(index) * g.vertex_stride;
    std::memcpy(&p[0], row + 0, sizeof(float));
    std::memcpy(&p[1], row + 4, sizeof(float));
    std::memcpy(&p[2], row + 8, sizeof(float));
}

double DistanceFromCenter(const xraylevel12::Geometry& g, uint32_t index)
{
    float p[3] = {0.0f, 0.0f, 0.0f};
    GetVertexPosition(g, index, p);
    const double dx = static_cast<double>(p[0]) - static_cast<double>(g.sphere.center[0]);
    const double dy = static_cast<double>(p[1]) - static_cast<double>(g.sphere.center[1]);
    const double dz = static_cast<double>(p[2]) - static_cast<double>(g.sphere.center[2]);
    return std::hypot(dx, dy, dz);
}

bool SphereCoversAllVertices(const xraylevel12::Geometry& g)
{
    if (g.vertex_count == 0 || g.vertex_stride != 32 ||
        !std::isfinite(g.sphere.radius) || g.sphere.radius < 0.0f)
        return false;
    for (uint32_t i = 0; i < g.vertex_count; ++i)
    {
        const double distance = DistanceFromCenter(g, i);
        if (!std::isfinite(distance) || distance > static_cast<double>(g.sphere.radius))
            return false;
    }
    return true;
}

double MaxDistanceFromPublishedCenter(const xraylevel12::Geometry& g)
{
    double maximum = 0.0;
    for (uint32_t i = 0; i < g.vertex_count; ++i)
        maximum = std::max(maximum, DistanceFromCenter(g, i));
    return maximum;
}

std::string Sha256Hex(const Bytes& input)
{
    static constexpr uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    Bytes message = input;
    const uint64_t bit_length = static_cast<uint64_t>(message.size()) * 8ull;
    message.push_back(0x80);
    while ((message.size() % 64) != 56)
        message.push_back(0);
    for (int i = 7; i >= 0; --i)
        message.push_back(static_cast<uint8_t>((bit_length >> (i * 8)) & 0xff));

    for (size_t chunk = 0; chunk < message.size(); chunk += 64)
    {
        uint32_t w[64] = {};
        for (int i = 0; i < 16; ++i)
        {
            const size_t p = chunk + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(message[p]) << 24) |
                   (static_cast<uint32_t>(message[p + 1]) << 16) |
                   (static_cast<uint32_t>(message[p + 2]) << 8) |
                   static_cast<uint32_t>(message[p + 3]);
        }
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = (w[i - 15] >> 7 | w[i - 15] << 25) ^
                                (w[i - 15] >> 18 | w[i - 15] << 14) ^
                                (w[i - 15] >> 3);
            const uint32_t s1 = (w[i - 2] >> 17 | w[i - 2] << 15) ^
                                (w[i - 2] >> 19 | w[i - 2] << 13) ^
                                (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t S1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^
                                (e >> 25 | e << 7);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^
                                (a >> 22 | a << 10);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(64);
    for (uint32_t value : h)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
            result.push_back(hex[(value >> shift) & 0xfu]);
    }
    return result;
}

Bytes IndexBytes(const std::vector<uint16_t>& indices)
{
    Bytes bytes;
    bytes.reserve(indices.size() * 2);
    for (uint16_t index : indices)
        AppendU16(bytes, index);
    return bytes;
}

std::span<const uint8_t> IndexByteView(const xraylevel12::Geometry& geometry)
{
    const std::span<const uint16_t> raw(geometry.indices.data(),
                                        geometry.indices.size());
    const auto bytes = std::as_bytes(raw);
    return std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

bool ReadFile(const char* path, Bytes& out)
{
    if (path == nullptr || *path == '\0')
        return false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0)
        return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (!out.empty())
        file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good() || file.eof();
}

struct TestContext
{
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    void Check(bool condition, const char* name, const std::string& detail = {})
    {
        if (condition)
        {
            ++passed;
            std::printf("[PASS] %s\n", name);
        }
        else
        {
            ++failed;
            std::printf("[FAIL] %s%s%s\n", name,
                        detail.empty() ? "" : " : ", detail.c_str());
        }
    }

    void Skip(const char* name, const std::string& detail)
    {
        ++skipped;
        std::printf("[SKIP] %s : %s\n", name, detail.c_str());
    }
};

std::string ErrorDetail(xraylevel12::ReadError error, xraylevel12::ReadError expected,
                        bool empty, const std::string& reason)
{
    return "error=" + std::to_string(static_cast<int>(error)) +
           " expected=" + std::to_string(static_cast<int>(expected)) +
           " empty=" + std::to_string(empty ? 1 : 0) + " reason=" + reason;
}

void ExpectFailure(TestContext& ctx, const char* name, const Bytes& level,
                   const Bytes& geom, xraylevel12::ReadError expected,
                   const char* expected_reason,
                   const xraylevel12::Limits& limits = xraylevel12::Limits{},
                   bool require_reason = true)
{
    xraylevel12::Geometry output;
    output.vertex_bytes.assign({0xde, 0xad, 0xbe, 0xef});
    output.indices.assign({0xffff});
    std::string reason = "stale";
    const xraylevel12::ReadError error = xraylevel12::ReadVisual(
        std::span<const uint8_t>(level.data(), level.size()),
        std::span<const uint8_t>(geom.data(), geom.size()),
        kVisualId, output, reason, limits);
    const bool reason_ok = !require_reason ||
        (!reason.empty() &&
         (expected_reason == nullptr || reason.find(expected_reason) != std::string::npos));
    ctx.Check(error == expected && output.empty() && reason_ok, name,
              ErrorDetail(error, expected, output.empty(), reason));
}

void ExpectSuccess(TestContext& ctx, const char* name, const Bytes& level,
                   const Bytes& geom, xraylevel12::Geometry& output,
                   const xraylevel12::Limits& limits = xraylevel12::Limits{})
{
    std::string reason = "stale";
    const xraylevel12::ReadError error = xraylevel12::ReadVisual(
        std::span<const uint8_t>(level.data(), level.size()),
        std::span<const uint8_t>(geom.data(), geom.size()),
        kVisualId, output, reason, limits);
    ctx.Check(error == xraylevel12::ReadError::None && reason.empty() && !output.empty(),
              name, ErrorDetail(error, xraylevel12::ReadError::None,
                                output.empty(), reason));
}

void TestSyntheticContract(TestContext& ctx)
{
    const Fixture f = BuildFixture();
    xraylevel12::Geometry g;
    ExpectSuccess(ctx, "synthetic valid extraction", f.level, f.geom, g);
    if (g.empty())
        return;

    ctx.Check(g.visual_id == kVisualId && g.shader_id == kShaderId,
              "visual id and shader provenance");
    ctx.Check(g.source_vb_id == 0 && g.source_vbase == 0 && g.source_vcount == 4,
              "source VB provenance");
    ctx.Check(g.source_ib_id == 0 && g.source_ibase == 0 && g.source_icount == 6,
              "source IB provenance");
    ctx.Check(g.vertex_stride == 32 && g.vertex_count == 4 &&
              g.vertex_bytes.size() == 128,
              "compact vertex size and stride");
    ctx.Check(g.index_count == 6 && g.indices.size() == 6 &&
              g.triangle_count == 2,
              "compact index size and triangle count");
    ctx.Check(g.first_index == 0 && g.base_vertex == 0,
              "compact draw contract is zero-based");
    ctx.Check(g.indices == std::vector<uint16_t>({0, 1, 2, 2, 1, 3}),
              "indices remain base-relative");
    ctx.Check(g.source_vertex_byte_offset == f.vb_data,
              "source vertex byte offset");
    ctx.Check(g.source_index_byte_offset == f.ib_data,
              "source index byte offset");
    ctx.Check(g.declaration_count == kDeclarationElements &&
              g.declaration[0].offset == 0 &&
              g.declaration[0].type == 2 &&
              g.declaration[1].offset == 12 &&
              g.declaration[1].type == 4 &&
              g.declaration[2].offset == 16 &&
              g.declaration[2].type == 4 &&
              g.declaration[3].offset == 20 &&
              g.declaration[3].type == 4 &&
              g.declaration[4].offset == 24 &&
              g.declaration[4].type == 4 &&
              g.declaration[5].offset == 28 &&
              g.declaration[5].type == 6 &&
              g.declaration[6].stream == 0xff &&
              g.declaration[6].offset == 0 &&
              g.declaration[6].type == kD3dDeclTypeUnused &&
              g.declaration[6].method == 0 &&
              g.declaration[6].usage == 0 &&
              g.declaration[6].usage_index == 0,
              "exact selected declaration and canonical D3DDECL_END");
    ctx.Check(g.header_bounds.minimum[0] <= g.bounds.minimum[0] &&
              g.header_bounds.maximum[0] >= g.bounds.maximum[0] &&
              g.header_bounds.minimum[1] <= g.bounds.minimum[1] &&
              g.header_bounds.maximum[1] >= g.bounds.maximum[1],
              "header AABB is only a containing bound");
    ctx.Check(SphereCoversAllVertices(g), "computed sphere covers all vertices");
    ctx.Check(g.vertex_bytes[0] == 0 && g.vertex_bytes[4] == 0 &&
              g.vertex_bytes[12] == static_cast<uint8_t>(0x80 + 12),
              "non-position attributes remain raw");
}

void TestOffsetContract(TestContext& ctx)
{
    const Fixture f = BuildFixture(2, 4, 2, 6, 6, 10);
    xraylevel12::Geometry g;
    ExpectSuccess(ctx, "extraction with non-zero source bases", f.level, f.geom, g);
    if (g.empty())
        return;

    ctx.Check(g.source_vbase == 2 && g.source_vcount == 4,
              "source vBase preserved");
    ctx.Check(g.source_ibase == 2 && g.source_icount == 6,
              "source iBase preserved");
    ctx.Check(g.first_index == 0 && g.base_vertex == 0,
              "compact draw offsets are zero despite source bases");
    ctx.Check(g.indices == std::vector<uint16_t>({0, 1, 2, 2, 1, 3}),
              "source-base-relative indices are not double-offset");
    ctx.Check(g.source_vertex_byte_offset == f.vb_data + 2 * 32,
              "source vertex byte offset includes vBase");
    ctx.Check(g.source_index_byte_offset == f.ib_data + 2 * 2,
              "source index byte offset includes iBase");
    ctx.Check(g.vertex_bytes.size() == 128 && g.index_count == 6,
              "compact size with non-zero source bases");

    float x = 0.0f;
    std::memcpy(&x, g.vertex_bytes.data(), sizeof(x));
    ctx.Check(x == 2.0f, "compact vertex data starts at source vBase");
}

void TestBoundsAndHeaderSphere(TestContext& ctx)
{
    const Fixture f = BuildFixture();
    xraylevel12::Geometry g;
    ExpectSuccess(ctx, "header AABB need not equal computed AABB", f.level, f.geom, g);
    if (g.empty())
        return;
    ctx.Check(g.header_bounds.minimum[0] < g.bounds.minimum[0] &&
              g.header_bounds.maximum[0] > g.bounds.maximum[0],
              "header bound is conservative");
    ctx.Check(g.sphere.radius > 0.0f &&
              g.sphere.center[0] >= g.bounds.minimum[0] &&
              g.sphere.center[0] <= g.bounds.maximum[0],
              "header sphere is not used");
    ctx.Check(SphereCoversAllVertices(g), "bounds sphere covers all vertices");
}

void TestSphereRegressions(TestContext& ctx)
{
    {
        Fixture f = BuildFixture();
        SetHeaderBounds(f.level, f, -2.0f, -2.0f, -2.0f, 3.0f, 3.0f, 3.0f);
        SetVertexPosition(f.geom, f, 0, 1.0f, 0.0f, 0.0f);
        SetVertexPosition(f.geom, f, 1, std::nextafter(1.0f, 2.0f), 0.0f, 0.0f);
        SetVertexPosition(f.geom, f, 2, 1.0f, std::nextafter(1.0f, 2.0f), 0.0f);
        SetVertexPosition(f.geom, f, 3, std::nextafter(1.0f, 2.0f),
                          std::nextafter(1.0f, 2.0f), 0.0f);
        xraylevel12::Geometry g;
        ExpectSuccess(ctx, "sphere nextafter float neighbors", f.level, f.geom, g);
        if (!g.empty())
        {
            ctx.Check(SphereCoversAllVertices(g),
                      "sphere nextafter covers published float center");
            ctx.Check(static_cast<double>(g.sphere.radius) >=
                      MaxDistanceFromPublishedCenter(g),
                      "sphere radius includes upward rounding");
        }
    }

    {
        Fixture f = BuildFixture();
        SetHeaderBounds(f.level, f, -2.0f, -2.0f, -2.0f, 2.0f, 2.0f, 2.0f);
        SetVertexPosition(f.geom, f, 0, 0.125f, 0.25f, 0.5f);
        SetVertexPosition(f.geom, f, 1, 1.0f, 0.0f, 0.0f);
        SetVertexPosition(f.geom, f, 2, 0.0f, 1.0f, 0.0f);
        SetVertexPosition(f.geom, f, 3, 0.0f, 0.0f, 1.0f);
        xraylevel12::Geometry g;
        ExpectSuccess(ctx, "sphere ordinary rounding direction", f.level, f.geom, g);
        if (!g.empty())
        {
            const double max_distance = MaxDistanceFromPublishedCenter(g);
            ctx.Check(SphereCoversAllVertices(g),
                      "sphere ordinary case covers all vertices");
            ctx.Check(static_cast<double>(g.sphere.radius) >= max_distance,
                      "sphere ordinary case rounds radius upward");
        }
    }

    {
        Fixture f = BuildFixture();
        SetHeaderBounds(f.level, f, -20.0f, -20.0f, -20.0f, 20.0f, 20.0f, 20.0f);
        SetVertexPosition(f.geom, f, 0, 1.25f, -3.5f, 2.75f);
        SetVertexPosition(f.geom, f, 1, -4.125f, 5.5f, -6.25f);
        SetVertexPosition(f.geom, f, 2, 7.75f, -8.5f, 9.25f);
        SetVertexPosition(f.geom, f, 3, -10.5f, 11.25f, -12.75f);
        xraylevel12::Geometry g;
        ExpectSuccess(ctx, "sphere multiple X/Y/Z coordinates", f.level, f.geom, g);
        if (!g.empty())
            ctx.Check(SphereCoversAllVertices(g),
                      "sphere multiple X/Y/Z covers all vertices");
    }

    {
        Fixture f = BuildFixture();
        SetHeaderBounds(f.level, f,
                        -std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max());
        SetVertexPosition(f.geom, f, 0,
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
        SetVertexPosition(f.geom, f, 1,
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max());
        SetVertexPosition(f.geom, f, 2, 0.0f, 0.0f, 0.0f);
        SetVertexPosition(f.geom, f, 3, 0.0f, 0.0f, 0.0f);
        ExpectFailure(ctx, "sphere ±FLT_MAX unrepresentable", f.level, f.geom,
                      xraylevel12::ReadError::InvalidComputedSphere,
                      "invalid computed sphere");
    }
}

void TestDeclarationTerminator(TestContext& ctx)
{
    const Fixture base = BuildFixture();
    const size_t terminator = base.vb_declaration + 6 * 8;

    {
        Bytes geom = base.geom;
        WriteU16(geom, terminator + 2, 1);
        ExpectFailure(ctx, "canonical D3DDECL_END offset field", base.level, geom,
                      xraylevel12::ReadError::InvalidDeclarationTerminatorFields,
                      "invalid D3DDECL_END fields");
    }
    {
        Bytes geom = base.geom;
        geom[terminator + 4] = 0;
        ExpectFailure(ctx, "canonical D3DDECL_END type field", base.level, geom,
                      xraylevel12::ReadError::InvalidDeclarationTerminatorFields,
                      "invalid D3DDECL_END fields");
    }
    {
        Bytes geom = base.geom;
        geom[terminator + 5] = 1;
        ExpectFailure(ctx, "canonical D3DDECL_END method field", base.level, geom,
                      xraylevel12::ReadError::InvalidDeclarationTerminatorFields,
                      "invalid D3DDECL_END fields");
    }
    {
        Bytes geom = base.geom;
        geom[terminator + 6] = 1;
        ExpectFailure(ctx, "canonical D3DDECL_END usage field", base.level, geom,
                      xraylevel12::ReadError::InvalidDeclarationTerminatorFields,
                      "invalid D3DDECL_END fields");
    }
    {
        Bytes geom = base.geom;
        geom[terminator + 7] = 1;
        ExpectFailure(ctx, "canonical D3DDECL_END usage index field", base.level, geom,
                      xraylevel12::ReadError::InvalidDeclarationTerminatorFields,
                      "invalid D3DDECL_END fields");
    }
    {
        Bytes declaration_only;
        AppendU32(declaration_only, 1);
        declaration_only.insert(declaration_only.end(),
                                base.geom.begin() + static_cast<ptrdiff_t>(base.vb_declaration),
                                base.geom.begin() + static_cast<ptrdiff_t>(base.vb_declaration + 6 * 8));
        Bytes malformed;
        AppendChunk(malformed, 9, declaration_only);
        AppendChunk(malformed, 10, Bytes{0, 0, 0, 0});
        ExpectFailure(ctx, "missing canonical D3DDECL_END", base.level, malformed,
                      xraylevel12::ReadError::InvalidDeclarationTerminator,
                      "invalid vertex declaration terminator");
    }
}

void TestCompressionFlags(TestContext& ctx)
{
    const Fixture base = BuildFixture();
    const auto expect_compressed = [&](const char* name, const Bytes& level,
                                       const Bytes& geom) {
        ExpectFailure(ctx, name, level, geom,
                      xraylevel12::ReadError::CompressedChunk,
                      "compressed chunk flags are not supported");
    };

    {
        Bytes level = base.level;
        SetChunkCompressed(level, base.level_visuals_header);
        expect_compressed("compressed fsL_VISUALS", level, base.geom);
    }
    {
        Bytes level = base.level;
        SetChunkCompressed(level, base.visual_chunk_header);
        expect_compressed("compressed visual id chunk", level, base.geom);
    }
    {
        Bytes level = base.level;
        SetChunkCompressed(level, base.header_header);
        expect_compressed("compressed OGF_HEADER", level, base.geom);
    }
    {
        Bytes level = base.level;
        SetChunkCompressed(level, base.container_header);
        expect_compressed("compressed OGF_GCONTAINER", level, base.geom);
    }
    {
        Bytes level = base.level;
        SetChunkCompressed(level, base.fastpath_header);
        expect_compressed("compressed ignored OGF_FASTPATH", level, base.geom);
    }
    {
        Bytes geom = base.geom;
        SetChunkCompressed(geom, base.vb_header);
        expect_compressed("compressed fsL_VB", base.level, geom);
    }
    {
        Bytes geom = base.geom;
        SetChunkCompressed(geom, base.ib_header);
        expect_compressed("compressed fsL_IB", base.level, geom);
    }
}

void TestAliasing(TestContext& ctx)
{
    const Fixture f = BuildFixture();
    xraylevel12::Geometry valid;
    ExpectSuccess(ctx, "aliasing baseline extraction", f.level, f.geom, valid);
    if (valid.empty())
        return;

    const auto expect_alias = [&](const char* name,
                                  std::span<const uint8_t> level,
                                  std::span<const uint8_t> geom,
                                  xraylevel12::Geometry& output,
                                  std::string& reason) {
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            level, geom, kVisualId, output, reason);
        ctx.Check(error == xraylevel12::ReadError::InputOutputAliasing &&
                  output.empty(),
                  name, ErrorDetail(error, xraylevel12::ReadError::InputOutputAliasing,
                                    output.empty(), reason));
    };

    {
        xraylevel12::Geometry output = valid;
        std::string reason = "stale";
        expect_alias("level aliases previous output.vertex_bytes",
                     std::span<const uint8_t>(output.vertex_bytes.data(),
                                              output.vertex_bytes.size()),
                     std::span<const uint8_t>(f.geom.data(), f.geom.size()),
                     output, reason);
    }
    {
        xraylevel12::Geometry output = valid;
        std::string reason = "stale";
        expect_alias("geom aliases previous output.vertex_bytes",
                     std::span<const uint8_t>(f.level.data(), f.level.size()),
                     std::span<const uint8_t>(output.vertex_bytes.data(),
                                              output.vertex_bytes.size()),
                     output, reason);
    }
    {
        xraylevel12::Geometry output = valid;
        std::string reason = "stale";
        expect_alias("input aliases previous output.indices",
                     std::span<const uint8_t>(
                         reinterpret_cast<const uint8_t*>(output.indices.data()),
                         output.indices.size() * sizeof(uint16_t)),
                     std::span<const uint8_t>(f.geom.data(), f.geom.size()),
                     output, reason);
    }
    {
        xraylevel12::Geometry output;
        std::string reason(256, 'r');
        expect_alias("input aliases previous reason storage",
                     std::span<const uint8_t>(
                         reinterpret_cast<const uint8_t*>(reason.data()),
                         reason.size()),
                     std::span<const uint8_t>(f.geom.data(), f.geom.size()),
                     output, reason);
    }
    {
        xraylevel12::Geometry output;
        std::string reason = "stale";
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            std::span<const uint8_t>(f.level.data(), f.level.size()),
            std::span<const uint8_t>(f.geom.data(), f.geom.size()),
            kVisualId, output, reason);
        ctx.Check(error == xraylevel12::ReadError::None && reason.empty() && !output.empty(),
                  "aliasing independent inputs still succeed",
                  ErrorDetail(error, xraylevel12::ReadError::None,
                              output.empty(), reason));
    }
}

void TestIndexAliasingCoverage(TestContext& ctx)
{
    // Не менее 24 индексов: вторая половина initialized byte range не должна
    // теряться при проверке input/output aliasing.
    const Fixture f = BuildFixture(0, 8, 0, 24, 8, 24);
    xraylevel12::Geometry valid;
    ExpectSuccess(ctx, "index aliasing fixture with 24 indices", f.level, f.geom, valid);
    if (valid.empty() || valid.indices.size() < 24)
    {
        ctx.Check(false, "index aliasing fixture size",
                  "expected at least 24 indices");
        return;
    }

    constexpr size_t probe_bytes = sizeof(uint16_t) * 4;
    const size_t second_half = (valid.indices.size() * sizeof(uint16_t)) / 2;

    {
        xraylevel12::Geometry output = valid;
        const std::span<const uint8_t> output_bytes = IndexByteView(output);
        std::string reason = "stale";
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            output_bytes.subspan(second_half, probe_bytes),
            std::span<const uint8_t>(f.geom.data(), f.geom.size()),
            kVisualId, output, reason);
        ctx.Check(error == xraylevel12::ReadError::InputOutputAliasing &&
                  output.empty(),
                  "level points into second half of output.indices",
                  ErrorDetail(error, xraylevel12::ReadError::InputOutputAliasing,
                              output.empty(), reason));
    }

    {
        xraylevel12::Geometry output = valid;
        const std::span<const uint8_t> output_bytes = IndexByteView(output);
        std::string reason = "stale";
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            std::span<const uint8_t>(f.level.data(), f.level.size()),
            output_bytes.subspan(second_half, probe_bytes),
            kVisualId, output, reason);
        ctx.Check(error == xraylevel12::ReadError::InputOutputAliasing &&
                  output.empty(),
                  "geom points into second half of output.indices",
                  ErrorDetail(error, xraylevel12::ReadError::InputOutputAliasing,
                              output.empty(), reason));
    }

    {
        xraylevel12::Geometry output;
        std::string reason = "stale";
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            std::span<const uint8_t>(f.level.data(), f.level.size()),
            std::span<const uint8_t>(f.geom.data(), f.geom.size()),
            kVisualId, output, reason);
        ctx.Check(error == xraylevel12::ReadError::None && reason.empty() &&
                  !output.empty() && output.indices.size() == 24,
                  "index aliasing independent input buffers",
                  ErrorDetail(error, xraylevel12::ReadError::None,
                              output.empty(), reason));
    }

    // Capacity > size probes must stay inside the initialized byte range.
    // The exact capacity is implementation-defined, so select a controlled
    // fixture rather than assuming reserve(n + 1) returns exactly n + 1.
    bool capacity_found = false;
    Fixture capacity_fixture;
    xraylevel12::Geometry capacity_level;
    xraylevel12::Geometry capacity_geom;
    size_t level_offset = 0;
    size_t geom_offset = 0;
    const uint32_t candidate_counts[] = {24, 32, 64, 128, 256};
    for (uint32_t count : candidate_counts)
    {
        Fixture candidate = (count == 24)
            ? f
            : BuildFixture(0, 8, 0, count, 8, count);
        xraylevel12::Geometry candidate_level;
        std::string reason = "stale";
        const xraylevel12::ReadError candidate_error = xraylevel12::ReadVisual(
            std::span<const uint8_t>(candidate.level.data(), candidate.level.size()),
            std::span<const uint8_t>(candidate.geom.data(), candidate.geom.size()),
            kVisualId, candidate_level, reason);
        if (candidate_error != xraylevel12::ReadError::None)
        {
            ctx.Check(false, "capacity probe candidate extraction",
                      ErrorDetail(candidate_error, xraylevel12::ReadError::None,
                                  candidate_level.empty(), reason));
            continue;
        }

        candidate_level.indices.reserve(candidate_level.indices.size() + 1);
        xraylevel12::Geometry candidate_geom = candidate_level;
        candidate_geom.indices.reserve(candidate_geom.indices.size() + 1);

        const size_t initialized_bytes =
            candidate_level.indices.size() * sizeof(uint16_t);
        const size_t candidate_level_offset = candidate_level.indices.capacity();
        const size_t candidate_geom_offset = candidate_geom.indices.capacity();
        if (candidate_level.indices.capacity() <= candidate_level.indices.size() ||
            candidate_geom.indices.capacity() <= candidate_geom.indices.size() ||
            candidate_level_offset > initialized_bytes ||
            candidate_geom_offset > initialized_bytes ||
            probe_bytes > initialized_bytes - candidate_level_offset ||
            probe_bytes > initialized_bytes - candidate_geom_offset)
            continue;

        capacity_fixture = std::move(candidate);
        capacity_level = std::move(candidate_level);
        capacity_geom = std::move(candidate_geom);
        level_offset = candidate_level_offset;
        geom_offset = candidate_geom_offset;
        capacity_found = true;
        break;
    }

    if (!capacity_found)
    {
        ctx.Check(false, "capacity probe fixture selection",
                  "no controlled fixture has an initialized probe range");
        return;
    }

    const size_t initialized_bytes =
        capacity_level.indices.size() * sizeof(uint16_t);
    ctx.Check(capacity_level.indices.capacity() > capacity_level.indices.size(),
              "index aliasing fixture capacity exceeds size");
    ctx.Check(level_offset >= capacity_level.indices.capacity(),
              "level capacity probe starts after old capacity range");
    ctx.Check(level_offset <= initialized_bytes,
              "level capacity probe starts within initialized bytes");
    if (level_offset <= initialized_bytes)
        ctx.Check(probe_bytes <= initialized_bytes - level_offset,
                  "level capacity probe stays within initialized bytes");
    ctx.Check(geom_offset >= capacity_geom.indices.capacity(),
              "geom capacity probe starts after old capacity range");
    ctx.Check(geom_offset <= initialized_bytes,
              "geom capacity probe starts within initialized bytes");
    if (geom_offset <= initialized_bytes)
        ctx.Check(probe_bytes <= initialized_bytes - geom_offset,
                  "geom capacity probe stays within initialized bytes");
    if (level_offset > initialized_bytes || geom_offset > initialized_bytes ||
        probe_bytes > initialized_bytes - level_offset ||
        probe_bytes > initialized_bytes - geom_offset)
        return;

    {
        const std::span<const uint8_t> level_bytes = IndexByteView(capacity_level);
        std::string reason = "stale";
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            level_bytes.subspan(level_offset, probe_bytes),
            std::span<const uint8_t>(capacity_fixture.geom.data(),
                                     capacity_fixture.geom.size()),
            kVisualId, capacity_level, reason);
        ctx.Check(error == xraylevel12::ReadError::InputOutputAliasing &&
                  capacity_level.empty(),
                  "level aliases initialized output.indices capacity range",
                  ErrorDetail(error, xraylevel12::ReadError::InputOutputAliasing,
                              capacity_level.empty(), reason));
    }

    {
        const std::span<const uint8_t> geom_bytes = IndexByteView(capacity_geom);
        std::string reason = "stale";
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            std::span<const uint8_t>(capacity_fixture.level.data(),
                                     capacity_fixture.level.size()),
            geom_bytes.subspan(geom_offset, probe_bytes),
            kVisualId, capacity_geom, reason);
        ctx.Check(error == xraylevel12::ReadError::InputOutputAliasing &&
                  capacity_geom.empty(),
                  "geom aliases initialized output.indices capacity range",
                  ErrorDetail(error, xraylevel12::ReadError::InputOutputAliasing,
                              capacity_geom.empty(), reason));
    }
}

void TestNegativeCases(TestContext& ctx)
{
    const Fixture base = BuildFixture();

    {
        Bytes level = base.level;
        level.resize(4);
        ExpectFailure(ctx, "truncated header", level, base.geom,
                      xraylevel12::ReadError::TruncatedChunkHeader,
                      "truncated chunk header");
    }
    {
        Bytes level = base.level;
        level.pop_back();
        ExpectFailure(ctx, "truncated chunk", level, base.geom,
                      xraylevel12::ReadError::ChunkExceedsInput,
                      "chunk exceeds input");
    }
    {
        Bytes level = base.level;
        Bytes duplicate;
        AppendChunk(duplicate, 3, Bytes{});
        level.insert(level.end(), duplicate.begin(), duplicate.end());
        ExpectFailure(ctx, "duplicate mandatory chunk", level, base.geom,
                      xraylevel12::ReadError::DuplicateMandatoryChunk,
                      "duplicate mandatory chunk");
    }
    {
        const Fixture no_gc = BuildFixture(0, 4, 0, 6, 4, 6, false);
        ExpectFailure(ctx, "missing GCONTAINER", no_gc.level, no_gc.geom,
                      xraylevel12::ReadError::MissingOgfGcContainer,
                      "missing OGF_GCONTAINER");
    }
    {
        Bytes level = base.level;
        level[base.header] = 3;
        ExpectFailure(ctx, "invalid OGF version", level, base.geom,
                      xraylevel12::ReadError::UnsupportedOgfVersion,
                      "unsupported OGF version");
    }
    {
        Bytes level = base.level;
        level[base.header + 1] = 11; // MT_TREE_PM, это не skeleton-тип
        ExpectFailure(ctx, "unsupported visual type", level, base.geom,
                      xraylevel12::ReadError::UnsupportedVisualType,
                      "unsupported visual type");
    }
    {
        Bytes level = base.level;
        WriteU32(level, base.container + 0, 99);
        ExpectFailure(ctx, "invalid source VB id", level, base.geom,
                      xraylevel12::ReadError::InvalidSourceVbId,
                      "invalid source VB id");
    }
    {
        Bytes level = base.level;
        WriteU32(level, base.container + 12, 99);
        ExpectFailure(ctx, "invalid source IB id", level, base.geom,
                      xraylevel12::ReadError::InvalidSourceIbId,
                      "invalid source IB id");
    }
    {
        Bytes level = base.level;
        WriteU32(level, base.container + 16, 0xfffffff0u);
        ExpectFailure(ctx, "overflow source index base", level, base.geom,
                      xraylevel12::ReadError::IndexRangeOutsideIb,
                      "visual index range");
    }
    {
        Bytes geom = base.geom;
        WriteU32(geom, base.vb_count, 0xffffffffu);
        ExpectFailure(ctx, "overflow source vertex count", base.level, geom,
                      xraylevel12::ReadError::InvalidVbCountOrStride,
                      "invalid VB vertex count/stride");
    }
    {
        Bytes geom = base.geom;
        WriteU16(geom, base.vb_declaration + 0, 1);
        ExpectFailure(ctx, "unsupported declaration stream", base.level, geom,
                      xraylevel12::ReadError::UnsupportedDeclarationElement,
                      "unsupported vertex declaration element");
    }
    {
        Bytes geom = base.geom;
        geom[base.vb_declaration + 5] = 1;
        ExpectFailure(ctx, "unsupported declaration method", base.level, geom,
                      xraylevel12::ReadError::UnsupportedDeclarationElement,
                      "unsupported vertex declaration element");
    }
    {
        Bytes geom = base.geom;
        geom[base.vb_declaration + 4] = 99;
        ExpectFailure(ctx, "unsupported declaration type", base.level, geom,
                      xraylevel12::ReadError::UnsupportedDeclarationElement,
                      "unsupported vertex declaration element");
    }
    {
        Bytes level = base.level;
        WriteU32(level, base.container + 4, 0xfffffff0u);
        ExpectFailure(ctx, "overflow source vertex base", level, base.geom,
                      xraylevel12::ReadError::VertexRangeOutsideVb,
                      "visual vertex range");
    }
    {
        Bytes level = base.level;
        WriteU32(level, base.container + 8, 5);
        ExpectFailure(ctx, "invalid source vertex range", level, base.geom,
                      xraylevel12::ReadError::VertexRangeOutsideVb,
                      "visual vertex range");
    }
    {
        Bytes level = base.level;
        WriteU32(level, base.container + 20, 5);
        ExpectFailure(ctx, "iCount not divisible by 3", level, base.geom,
                      xraylevel12::ReadError::InvalidVisualCounts,
                      "invalid visual counts");
    }
    {
        Bytes geom = base.geom;
        WriteU16(geom, base.ib_data, 0xffff);
        ExpectFailure(ctx, "invalid index", base.level, geom,
                      xraylevel12::ReadError::IndexOutsideVertexRange,
                      "index outside compact vertex range");
    }
    {
        Bytes geom = base.geom;
        const uint32_t nan_bits = 0x7fc00000u;
        std::memcpy(geom.data() + base.vb_data, &nan_bits, sizeof(nan_bits));
        ExpectFailure(ctx, "NaN position", base.level, geom,
                      xraylevel12::ReadError::NonFinitePosition,
                      "non-finite position data");
    }
    {
        Bytes geom = base.geom;
        const uint32_t inf_bits = 0x7f800000u;
        std::memcpy(geom.data() + base.vb_data, &inf_bits, sizeof(inf_bits));
        ExpectFailure(ctx, "Inf position", base.level, geom,
                      xraylevel12::ReadError::NonFinitePosition,
                      "non-finite position data");
    }
    {
        Bytes level = base.level;
        WriteF32(level, base.header + 4, 10.0f);
        WriteF32(level, base.header + 16, -10.0f);
        ExpectFailure(ctx, "invalid AABB", level, base.geom,
                      xraylevel12::ReadError::InvalidOgfHeaderBounds,
                      "invalid OGF_HEADER bounds");
    }
    {
        Bytes level = base.level;
        WriteF32(level, base.header + 16, 0.5f);
        ExpectFailure(ctx, "computed AABB outside header", level, base.geom,
                      xraylevel12::ReadError::ComputedBoundsOutsideHeader,
                      "computed bounds exceed header bounds");
    }
    {
        xraylevel12::Limits limits;
        limits.maxOutputBytes = 139;
        ExpectFailure(ctx, "output memory budget exceeded", base.level, base.geom,
                      xraylevel12::ReadError::OutputBudgetExceeded,
                      "output geometry exceeds size limit", limits);
    }
    {
        xraylevel12::Limits limits;
        limits.maxLevelBytes = 1;
        ExpectFailure(ctx, "level input budget exceeded", base.level, base.geom,
                      xraylevel12::ReadError::LevelInputTooLarge,
                      "level input exceeds size limit", limits);
    }
    {
        xraylevel12::Limits limits;
        limits.maxGeomBytes = 1;
        ExpectFailure(ctx, "level.geom input budget exceeded", base.level, base.geom,
                      xraylevel12::ReadError::GeomInputTooLarge,
                      "level.geom input exceeds size limit", limits);
    }
    {
        Bytes empty;
        ExpectFailure(ctx, "empty level input", empty, base.geom,
                      xraylevel12::ReadError::EmptyLevelInput,
                      "empty level input");
        ExpectFailure(ctx, "empty level.geom input", base.level, empty,
                      xraylevel12::ReadError::EmptyGeomInput,
                      "empty level.geom input");
    }
    {
        xraylevel12::Geometry output;
        output.vertex_bytes.assign({1, 2, 3});
        std::string reason = "stale";
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            std::span<const uint8_t>(base.level.data(), base.level.size()),
            std::span<const uint8_t>(base.geom.data(), base.geom.size()),
            99, output, reason);
        ctx.Check(error == xraylevel12::ReadError::VisualNotFound &&
                  output.empty() && reason.find("not found") != std::string::npos,
                  "missing visual id",
                  ErrorDetail(error, xraylevel12::ReadError::VisualNotFound,
                              output.empty(), reason));
    }
}

#ifdef XRAYLEVEL_OOM_TEST
void TestAllocationFailure(TestContext& ctx)
{
    const Fixture f = BuildFixture();
    xraylevel12::Geometry output;
    output.vertex_bytes.assign({1, 2, 3});
    output.indices.assign({0xffff});
    std::string reason = "stale";

    g_fail_next_allocation.store(true, std::memory_order_release);
    const xraylevel12::ReadError error = xraylevel12::ReadVisual(
        std::span<const uint8_t>(f.level.data(), f.level.size()),
        std::span<const uint8_t>(f.geom.data(), f.geom.size()),
        kVisualId, output, reason);
    g_fail_next_allocation.store(false, std::memory_order_release);

    ctx.Check(error == xraylevel12::ReadError::AllocationFailure && output.empty(),
              "allocation fault maps to status and empty output",
              ErrorDetail(error, xraylevel12::ReadError::AllocationFailure,
                          output.empty(), reason));
    ctx.Check(reason.empty() || reason.find("allocation") != std::string::npos,
              "allocation fault reason is best-effort", reason);
}
#endif

void TestRepeatedAndLifetime(TestContext& ctx)
{
    const Fixture base = BuildFixture();
    xraylevel12::Geometry first;
    ExpectSuccess(ctx, "repeated extraction first call", base.level, base.geom, first);
    const std::vector<uint8_t> first_vertices = first.vertex_bytes;
    const std::vector<uint16_t> first_indices = first.indices;

    xraylevel12::Geometry second;
    second.vertex_bytes.assign({0xaa});
    std::string second_reason = "stale";
    const xraylevel12::ReadError second_error = xraylevel12::ReadVisual(
        std::span<const uint8_t>(base.level.data(), base.level.size()),
        std::span<const uint8_t>(base.geom.data(), base.geom.size()),
        kVisualId, second, second_reason);
    ctx.Check(second_error == xraylevel12::ReadError::None &&
              second_reason.empty() &&
              second.vertex_bytes == first_vertices &&
              second.indices == first_indices,
              "repeated extraction success -> success");

    Bytes bad_geom = base.geom;
    WriteU16(bad_geom, base.ib_data, 0xffff);
    xraylevel12::Geometry third;
    third.vertex_bytes = first_vertices;
    third.indices = first_indices;
    std::string third_reason = "stale";
    const xraylevel12::ReadError third_error = xraylevel12::ReadVisual(
        std::span<const uint8_t>(base.level.data(), base.level.size()),
        std::span<const uint8_t>(bad_geom.data(), bad_geom.size()),
        kVisualId, third, third_reason);
    ctx.Check(third_error == xraylevel12::ReadError::IndexOutsideVertexRange &&
              third.empty() && !third_reason.empty(),
              "repeated extraction success -> failure cleanup", third_reason);

    xraylevel12::Geometry fourth;
    fourth.vertex_bytes.assign({0xbb});
    std::string fourth_reason = "stale";
    const xraylevel12::ReadError fourth_error = xraylevel12::ReadVisual(
        std::span<const uint8_t>(base.level.data(), base.level.size()),
        std::span<const uint8_t>(base.geom.data(), base.geom.size()),
        kVisualId, fourth, fourth_reason);
    ctx.Check(fourth_error == xraylevel12::ReadError::None &&
              fourth_reason.empty() &&
              fourth.vertex_bytes == first_vertices &&
              fourth.indices == first_indices,
              "repeated extraction failure -> success");

    xraylevel12::Geometry lifetime_output;
    {
        Bytes level = base.level;
        Bytes geom = base.geom;
        std::string reason;
        const xraylevel12::ReadError error = xraylevel12::ReadVisual(
            std::span<const uint8_t>(level.data(), level.size()),
            std::span<const uint8_t>(geom.data(), geom.size()),
            kVisualId, lifetime_output, reason);
        ctx.Check(error == xraylevel12::ReadError::None && reason.empty(),
                  "output lifetime source call");
    }
    ctx.Check(lifetime_output.vertex_bytes.size() == 128 &&
              lifetime_output.indices.size() == 6,
              "output remains owned after input buffers are freed");
}

void TestRealJupiter(TestContext& ctx, int argc, char** argv)
{
    const char* level_path = nullptr;
    const char* geom_path = nullptr;
    bool requested = false;

    if (argc > 3)
    {
        ctx.Check(false, "real Jupiter argument count",
                  "expected zero, two paths, or environment variables");
        return;
    }
    if (argc == 3)
    {
        requested = true;
        level_path = argv[1];
        geom_path = argv[2];
    }
    else if (argc == 2)
    {
        ctx.Check(false, "real Jupiter argument count",
                  "one path is not enough; provide both level and level.geom");
        return;
    }
    else
    {
        const char* env_level = std::getenv("XRAYLEVEL_LEVEL");
        const char* env_geom = std::getenv("XRAYLEVEL_GEOM");
        if (env_level != nullptr || env_geom != nullptr)
        {
            requested = true;
            level_path = env_level;
            geom_path = env_geom;
        }
    }

    if (!requested)
    {
        ctx.Skip("real Jupiter validation",
                 "assets not supplied; set XRAYLEVEL_LEVEL/XRAYLEVEL_GEOM or pass two paths");
        return;
    }
    if (level_path == nullptr || geom_path == nullptr ||
        *level_path == '\0' || *geom_path == '\0')
    {
        ctx.Check(false, "real Jupiter explicit asset paths",
                  "both level and level.geom paths are required");
        return;
    }

    Bytes level;
    Bytes geom;
    if (!ReadFile(level_path, level) || !ReadFile(geom_path, geom))
    {
        ctx.Check(false, "real Jupiter explicit asset read",
                  "input file is missing or unreadable");
        return;
    }

    const bool strict = std::getenv("XRAYLEVEL_STRICT") != nullptr;
    if (strict)
    {
        const std::string level_hash = Sha256Hex(level);
        const std::string geom_hash = Sha256Hex(geom);
        ctx.Check(level_hash == "D91998F5B906F2C92F443033F4E9D7F096E307696F4C1FACB51FDEDA4A54EE9E",
                  "real Jupiter strict level SHA-256", level_hash);
        ctx.Check(geom_hash == "BBF30871726CEF8311239CFC0A3CD155667F9E527F8701ACDE1C0A8FDB6178A8",
                  "real Jupiter strict level.geom SHA-256", geom_hash);
    }

    xraylevel12::Geometry output;
    std::string reason;
    const xraylevel12::ReadError error = xraylevel12::ReadVisual(
        std::span<const uint8_t>(level.data(), level.size()),
        std::span<const uint8_t>(geom.data(), geom.size()),
        3, output, reason);
    if (error != xraylevel12::ReadError::None)
    {
        ctx.Check(false, "real Jupiter visual 3 extraction",
                  ErrorDetail(error, xraylevel12::ReadError::None,
                              output.empty(), reason));
        return;
    }

    ctx.Check(reason.empty() && output.visual_id == 3 && output.shader_id == 7,
              "real Jupiter visual 3 id/type");
    ctx.Check(output.source_vb_id == 15 && output.source_vbase == 27220 &&
              output.source_vcount == 120,
              "real Jupiter VB provenance");
    ctx.Check(output.source_ib_id == 5 && output.source_ibase == 219519 &&
              output.source_icount == 192,
              "real Jupiter IB provenance");
    ctx.Check(output.vertex_stride == 32 && output.vertex_bytes.size() == 3840 &&
              output.indices.size() == 192 && output.triangle_count == 64,
              "real Jupiter compact sizes");
    ctx.Check(output.first_index == 0 && output.base_vertex == 0,
              "real Jupiter compact draw offsets");
    ctx.Check(output.declaration_count == 7 &&
              output.declaration[0].type == 2 &&
              output.declaration[1].offset == 12 &&
              output.declaration[2].offset == 16 &&
              output.declaration[3].offset == 20 &&
              output.declaration[4].offset == 24 &&
              output.declaration[5].offset == 28 &&
              output.declaration[6].stream == 0xff &&
              output.declaration[6].type == kD3dDeclTypeUnused &&
              output.declaration[6].offset == 0 &&
              output.declaration[6].method == 0 &&
              output.declaration[6].usage == 0 &&
              output.declaration[6].usage_index == 0,
              "real Jupiter selected declaration and D3DDECL_END");
    ctx.Check(SphereCoversAllVertices(output),
              "real Jupiter computed sphere covers all vertices");
    ctx.Check(output.header_bounds.minimum[0] <= output.bounds.minimum[0] &&
              output.header_bounds.maximum[0] >= output.bounds.maximum[0] &&
              output.header_bounds.minimum[1] <= output.bounds.minimum[1] &&
              output.header_bounds.maximum[1] >= output.bounds.maximum[1] &&
              output.header_bounds.minimum[2] <= output.bounds.minimum[2] &&
              output.header_bounds.maximum[2] >= output.bounds.maximum[2],
              "real Jupiter header AABB contains computed AABB");
    ctx.Check(output.source_vertex_byte_offset + output.vertex_bytes.size() <= geom.size() &&
              output.source_index_byte_offset + output.indices.size() * 2 <= geom.size(),
              "real Jupiter source ranges are bounded");

    const std::string vertex_hash = Sha256Hex(output.vertex_bytes);
    const std::string index_hash = Sha256Hex(IndexBytes(output.indices));
    ctx.Check(vertex_hash == "E62D2A6ED45D95997BD4C5DE66AF1E731A4296FAD008D39EAE31B274099D5841",
              "real Jupiter compact vertex SHA-256", vertex_hash);
    ctx.Check(index_hash == "BE2A2F3B75643ABA50BF27608A358BA83E08D4CE8E027F5152CB3D48F4FF3AB7",
              "real Jupiter compact index SHA-256", index_hash);
}
} // namespace

int main(int argc, char** argv)
{
    TestContext ctx;
    std::printf("F4.0-B1 CPU geometry reader hardening tests\n");
    TestSyntheticContract(ctx);
    TestOffsetContract(ctx);
    TestBoundsAndHeaderSphere(ctx);
    TestSphereRegressions(ctx);
    TestDeclarationTerminator(ctx);
    TestCompressionFlags(ctx);
    TestAliasing(ctx);
    TestIndexAliasingCoverage(ctx);
    TestNegativeCases(ctx);
#ifdef XRAYLEVEL_OOM_TEST
    TestAllocationFailure(ctx);
#endif
    TestRepeatedAndLifetime(ctx);
    TestRealJupiter(ctx, argc, argv);
    std::printf("SUMMARY passed=%d failed=%d skipped=%d\n",
                ctx.passed, ctx.failed, ctx.skipped);
    return ctx.failed == 0 ? 0 : 1;
}
