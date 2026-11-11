#define NOMINMAX
#include "dx12xraylevelasset.h"
#include "dx12xraylevelgeometry.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace
{
using Clock = std::chrono::steady_clock;

enum class Fault
{
    None,
    TruncatedRead,
    HashFailure,
    DeterminismFailure,
};

struct HashResult
{
    bool ok = false;
    std::string hex;
    std::string error;
};

double Ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}

HashResult Sha256HexWithAlgorithm(const wchar_t* algorithmName,
                                  const void* data, size_t size,
                                  Fault fault = Fault::None)
{
    HashResult result;
    if (fault == Fault::HashFailure)
    {
        result.error = "forced BCrypt failure";
        return result;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD resultSize = 0;
    std::vector<uint8_t> object;
    uint8_t digest[32]{};

    if (BCryptOpenAlgorithmProvider(&algorithm, algorithmName, nullptr, 0) < 0)
    {
        result.error = "BCryptOpenAlgorithmProvider failed";
        return result;
    }
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize), &resultSize, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        result.error = "BCryptGetProperty failed";
        return result;
    }
    object.resize(objectSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize,
                         nullptr, 0, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        result.error = "BCryptCreateHash failed";
        return result;
    }
    if (size != 0 &&
        BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(data)),
                       static_cast<ULONG>(size), 0) < 0)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        result.error = "BCryptHashData failed";
        return result;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        result.error = "BCryptFinishHash failed";
        return result;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    static const char* hex = "0123456789ABCDEF";
    result.hex.reserve(64);
    for (uint8_t byte : digest)
    {
        result.hex.push_back(hex[byte >> 4]);
        result.hex.push_back(hex[byte & 0x0F]);
    }
    result.ok = true;
    return result;
}

HashResult Sha256Hex(const void* data, size_t size, Fault fault = Fault::None)
{
    return Sha256HexWithAlgorithm(BCRYPT_SHA256_ALGORITHM, data, size, fault);
}

bool HashEqual(const HashResult& left, const HashResult& right)
{
    return left.ok && right.ok && left.hex == right.hex;
}

bool ReadExactBytes(std::istream& file, size_t expected,
                    std::vector<uint8_t>& bytes, size_t maxBytes,
                    std::string& reason)
{
    if (expected == 0)
    {
        reason = "empty or invalid file size";
        return false;
    }
    if (expected > maxBytes)
    {
        reason = "file exceeds read limit";
        return false;
    }
    bytes.resize(expected);
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(expected));
    if (file.gcount() != static_cast<std::streamsize>(expected) ||
        file.bad())
    {
        bytes.clear();
        reason = "incomplete file read";
        return false;
    }
    return true;
}

bool ReadFileBytes(const char* path, std::vector<uint8_t>& bytes,
                   size_t maxBytes, std::string& reason)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        reason = "cannot open file";
        return false;
    }
    const std::streamoff length = file.tellg();
    if (length <= 0)
    {
        reason = "empty or invalid file size";
        return false;
    }
    const size_t size = static_cast<size_t>(length);
    file.seekg(0, std::ios::beg);
    return ReadExactBytes(file, size, bytes, maxBytes, reason);
}

void PrintBounds(const char* label, const xraylevel12::Bounds& bounds)
{
    std::printf("%s_min=%.9g,%.9g,%.9g %s_max=%.9g,%.9g,%.9g\n", label,
                bounds.minimum[0], bounds.minimum[1], bounds.minimum[2],
                label, bounds.maximum[0], bounds.maximum[1],
                bounds.maximum[2]);
}

void PrintSphere(const xraylevel12::Sphere& sphere)
{
    std::printf("sphere_center=%.9g,%.9g,%.9g sphere_radius=%.9g\n",
                sphere.center[0], sphere.center[1], sphere.center[2],
                sphere.radius);
}

HashResult GeometryVertexHash(const xraylevel12::Geometry& geometry,
                              Fault fault)
{
    return Sha256Hex(geometry.vertex_bytes.data(),
                     geometry.vertex_bytes.size(), fault);
}

HashResult GeometryIndexHash(const xraylevel12::Geometry& geometry,
                             Fault fault)
{
    return Sha256Hex(geometry.indices.data(),
                     geometry.indices.size() * sizeof(uint16_t), fault);
}

HashResult AssetVertexHash(const XrayAssetData12& asset, Fault fault)
{
    return Sha256Hex(asset.vertices.data(),
                     asset.vertices.size() * sizeof(XrayVertex12), fault);
}

HashResult AssetIndexHash(const XrayAssetData12& asset, Fault fault)
{
    return Sha256Hex(asset.indices.data(),
                     asset.indices.size() * sizeof(uint16_t), fault);
}

struct VisualResult
{
    xraylevel12::ReadError readError = xraylevel12::ReadError::None;
    bool converted = false;
    bool hashOk = true;
    bool deterministic = false;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

bool SameGeometry(const xraylevel12::Geometry& a,
                  const xraylevel12::Geometry& b)
{
    return a.visual_id == b.visual_id && a.shader_id == b.shader_id &&
           a.source_visual_offset == b.source_visual_offset &&
           a.source_visual_size == b.source_visual_size &&
           a.source_vb_id == b.source_vb_id &&
           a.source_vbase == b.source_vbase &&
           a.source_vcount == b.source_vcount &&
           a.source_ib_id == b.source_ib_id &&
           a.source_ibase == b.source_ibase &&
           a.source_icount == b.source_icount &&
           a.source_vertex_byte_offset == b.source_vertex_byte_offset &&
           a.source_index_byte_offset == b.source_index_byte_offset &&
           a.first_index == b.first_index && a.base_vertex == b.base_vertex &&
           a.index_count == b.index_count &&
           a.triangle_count == b.triangle_count &&
           a.vertex_stride == b.vertex_stride &&
           a.vertex_count == b.vertex_count &&
           a.declaration_count == b.declaration_count &&
           a.vertex_bytes == b.vertex_bytes && a.indices == b.indices &&
           std::memcmp(&a.header_bounds, &b.header_bounds,
                       sizeof(a.header_bounds)) == 0 &&
           std::memcmp(&a.bounds, &b.bounds, sizeof(a.bounds)) == 0 &&
           std::memcmp(&a.sphere, &b.sphere, sizeof(a.sphere)) == 0;
}

VisualResult AnalyzeVisual(uint32_t visualId,
                           const std::vector<uint8_t>& levelBytes,
                           const std::vector<uint8_t>& geomBytes,
                           const xraylevel12::Limits& limits,
                           Fault fault = Fault::None)
{
    VisualResult result;
    xraylevel12::Geometry geometry;
    std::string reason;
    const Clock::time_point readStart = Clock::now();
    result.readError = xraylevel12::ReadVisual(levelBytes, geomBytes, visualId,
                                               geometry, reason, limits);
    const double readMs = Ms(readStart);
    if (result.readError != xraylevel12::ReadError::None)
    {
        std::printf("VISUAL %u READ=FAIL error=%s reason='%s' read_ms=%.3f\n",
                    visualId, xraylevel12::ReadErrorMessage(result.readError),
                    reason.c_str(), readMs);
        return result;
    }

    result.vertexCount = geometry.vertex_count;
    result.indexCount = geometry.index_count;

    const HashResult compactVb = GeometryVertexHash(geometry, fault);
    const HashResult compactIb = GeometryIndexHash(geometry, fault);
    if (!compactVb.ok || !compactIb.ok)
    {
        result.hashOk = false;
        std::printf("VISUAL %u SHA256_STATUS=FAIL compact_vb='%s' "
                    "compact_ib='%s'\n",
                    visualId,
                    compactVb.ok ? compactVb.hex.c_str()
                                 : compactVb.error.c_str(),
                    compactIb.ok ? compactIb.hex.c_str()
                                 : compactIb.error.c_str());
        return result;
    }

    uint16_t minIndex = 0xFFFF;
    uint16_t maxIndex = 0;
    for (uint16_t index : geometry.indices)
    {
        minIndex = std::min(minIndex, index);
        maxIndex = std::max(maxIndex, index);
    }

    std::printf("VISUAL %u READ=PASS read_ms=%.3f SHA256_STATUS=PASS\n",
                visualId, readMs);
    std::printf("visual_id=%u shader_id=%u source_visual_offset=%llu "
                "source_visual_size=%u\n",
                geometry.visual_id, geometry.shader_id,
                static_cast<unsigned long long>(geometry.source_visual_offset),
                geometry.source_visual_size);
    std::printf("source_vb_id=%u source_vbase=%u source_vcount=%u "
                "source_ib_id=%u source_ibase=%u source_icount=%u\n",
                geometry.source_vb_id, geometry.source_vbase,
                geometry.source_vcount, geometry.source_ib_id,
                geometry.source_ibase, geometry.source_icount);
    std::printf("source_vertex_byte_offset=%llu "
                "source_index_byte_offset=%llu\n",
                static_cast<unsigned long long>(
                    geometry.source_vertex_byte_offset),
                static_cast<unsigned long long>(
                    geometry.source_index_byte_offset));
    std::printf("stride=%u vertices=%u indices=%u triangles=%u "
                "first_index=%u base_vertex=%u declaration_count=%u\n",
                geometry.vertex_stride, geometry.vertex_count,
                geometry.index_count, geometry.triangle_count,
                geometry.first_index, geometry.base_vertex,
                geometry.declaration_count);
    for (uint32_t i = 0; i < geometry.declaration_count; ++i)
    {
        const auto& e = geometry.declaration[i];
        std::printf("decl[%u] stream=%u offset=%u type=%u method=%u "
                    "usage=%u usage_index=%u\n",
                    i, e.stream, e.offset, e.type, e.method, e.usage,
                    e.usage_index);
    }
    PrintBounds("header", geometry.header_bounds);
    PrintBounds("computed", geometry.bounds);
    PrintSphere(geometry.sphere);
    std::printf("compact_vb_bytes=%zu compact_ib_bytes=%zu "
                "min_index=%u max_index=%u\n",
                geometry.vertex_bytes.size(),
                geometry.indices.size() * sizeof(uint16_t), minIndex,
                maxIndex);
    std::printf("compact_vb_sha256=%s compact_ib_sha256=%s\n",
                compactVb.hex.c_str(), compactIb.hex.c_str());

    XrayAssetData12 asset;
    const char* convertReason = nullptr;
    const Clock::time_point convertStart = Clock::now();
    result.converted = BuildXrayLevelAsset12(geometry, asset, &convertReason);
    const double convertMs = Ms(convertStart);
    if (!result.converted)
    {
        std::printf("VISUAL %u CONVERT=FAIL reason='%s' convert_ms=%.3f\n",
                    visualId,
                    convertReason != nullptr ? convertReason : "", convertMs);
        return result;
    }

    const HashResult assetVb = AssetVertexHash(asset, fault);
    const HashResult assetIb = AssetIndexHash(asset, fault);
    if (!assetVb.ok || !assetIb.ok)
    {
        result.hashOk = false;
        std::printf("VISUAL %u SHA256_STATUS=FAIL asset_vb='%s' "
                    "asset_ib='%s'\n",
                    visualId,
                    assetVb.ok ? assetVb.hex.c_str() : assetVb.error.c_str(),
                    assetIb.ok ? assetIb.hex.c_str() : assetIb.error.c_str());
        return result;
    }

    std::printf("VISUAL %u CONVERT=PASS convert_ms=%.3f\n", visualId,
                convertMs);
    std::printf("asset_vertices=%zu asset_indices=%zu asset_vb_bytes=%zu "
                "asset_ib_bytes=%zu\n",
                asset.vertices.size(), asset.indices.size(),
                asset.vertices.size() * sizeof(XrayVertex12),
                asset.indices.size() * sizeof(uint16_t));
    std::printf("asset_min=%.9g,%.9g,%.9g asset_max=%.9g,%.9g,%.9g "
                "asset_sphere_radius=%.9g\n",
                asset.minimum.x, asset.minimum.y, asset.minimum.z,
                asset.maximum.x, asset.maximum.y, asset.maximum.z,
                asset.sphere.radius);
    std::printf("asset_vb_sha256=%s asset_ib_sha256=%s\n",
                assetVb.hex.c_str(), assetIb.hex.c_str());

    xraylevel12::Geometry second;
    std::string secondReason;
    const xraylevel12::ReadError secondError = xraylevel12::ReadVisual(
        levelBytes, geomBytes, visualId, second, secondReason, limits);
    XrayAssetData12 secondAsset;
    const char* secondConvertReason = nullptr;
    const bool secondConverted =
        secondError == xraylevel12::ReadError::None &&
        BuildXrayLevelAsset12(second, secondAsset, &secondConvertReason);
    bool deterministic =
        secondError == xraylevel12::ReadError::None && secondConverted &&
        SameGeometry(geometry, second) &&
        AssetVertexHash(secondAsset, fault).hex == assetVb.hex &&
        AssetIndexHash(secondAsset, fault).hex == assetIb.hex;
    if (fault == Fault::DeterminismFailure)
        deterministic = false;
    result.deterministic = deterministic;
    std::printf("VISUAL %u DETERMINISM=%s\n", visualId,
                deterministic ? "PASS" : "FAIL");
    return result;
}

int RunSelfTest(const char* levelPath, const char* geomPath)
{
    int failures = 0;

    // 1. A real short stream (gcount < requested) must fail and clear bytes.
    {
        const char shortData[4] = {1, 2, 3, 4};
        std::istringstream stream(std::string(shortData, sizeof(shortData)));
        std::vector<uint8_t> bytes;
        std::string reason;
        const bool truncated =
            !ReadExactBytes(stream, 8, bytes, 1024, reason) && bytes.empty();
        std::printf("HARNESS_NEGATIVE truncated_read=%s reason='%s'\n",
                    truncated ? "PASS" : "FAIL", reason.c_str());
        if (!truncated)
            ++failures;
    }

    // 2. BCrypt failure must be an explicit failed status, not a fake hash.
    {
        const HashResult result =
            Sha256Hex("x", 1, Fault::HashFailure);
        const bool failed = !result.ok && !result.error.empty() &&
                            result.hex.empty();
        std::printf("HARNESS_NEGATIVE hash_failure=%s error='%s'\n",
                    failed ? "PASS" : "FAIL", result.error.c_str());
        if (!failed)
            ++failures;
    }

    // 3. A forced determinism mismatch must be observable by the caller.
    {
        std::vector<uint8_t> levelBytes;
        std::vector<uint8_t> geomBytes;
        std::string reason;
        const bool read =
            ReadFileBytes(levelPath, levelBytes, 16ull * 1024ull * 1024ull,
                          reason) &&
            ReadFileBytes(geomPath, geomBytes, 128ull * 1024ull * 1024ull,
                          reason);
        if (!read)
        {
            std::printf("HARNESS_NEGATIVE determinism_failure=FAIL "
                        "reason='%s'\n", reason.c_str());
            ++failures;
        }
        else
        {
            const VisualResult result = AnalyzeVisual(
                9, levelBytes, geomBytes, xraylevel12::Limits{},
                Fault::DeterminismFailure);
            const bool failed = result.hashOk && result.converted &&
                                !result.deterministic;
            std::printf("HARNESS_NEGATIVE determinism_failure=%s\n",
                        failed ? "PASS" : "FAIL");
            if (!failed)
                ++failures;
        }
    }

    std::printf("M0_HARNESS_NEGATIVE_RESULT %s failures=%d\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv)
{
    const bool selfTest = argc > 1 && std::strcmp(argv[1], "--self-test") == 0;
    const bool negativeDeterminism =
        argc > 1 && std::strcmp(argv[1], "--negative-determinism") == 0;
    const bool negativeShortRead =
        argc > 1 && std::strcmp(argv[1], "--negative-short-read") == 0;
    const bool negativeHashFailure =
        argc > 1 && std::strcmp(argv[1], "--negative-hash-failure") == 0;

    if (negativeShortRead)
    {
        const char shortData[4] = {1, 2, 3, 4};
        std::istringstream stream(std::string(shortData, sizeof(shortData)));
        std::vector<uint8_t> bytes;
        std::string reason;
        const bool detected =
            !ReadExactBytes(stream, 8, bytes, 1024, reason) && bytes.empty();
        std::printf("M0_NEGATIVE_SHORT_READ %s reason='%s'\n",
                    detected ? "PASS" : "FAIL", reason.c_str());
        return detected ? 1 : 0;
    }

    if (negativeHashFailure)
    {
        const HashResult first =
            Sha256HexWithAlgorithm(L"INVALID-SHA-ALGORITHM", "x", 1);
        const HashResult second =
            Sha256HexWithAlgorithm(L"INVALID-SHA-ALGORITHM-2", "x", 1);
        const bool explicitFailure =
            !first.ok && !first.error.empty() && first.hex.empty();
        const bool notEqual = !HashEqual(first, second);
        std::printf("M0_NEGATIVE_HASH_FAILURE %s explicit=%d not_equal=%d "
                    "error='%s'\n",
                    (explicitFailure && notEqual) ? "PASS" : "FAIL",
                    explicitFailure ? 1 : 0, notEqual ? 1 : 0,
                    first.error.c_str());
        return (explicitFailure && notEqual) ? 1 : 0;
    }

    const bool needsInputs = !negativeShortRead && !negativeHashFailure;
    const int base = (selfTest || negativeDeterminism) ? 2 : 1;
    if (needsInputs && argc < base + 2)
    {
        std::printf("usage: %s [--self-test|--negative-determinism] "
                    "<level> <level.geom>\n"
                    "       %s --negative-short-read\n"
                    "       %s --negative-hash-failure\n",
                    argv[0], argv[0], argv[0]);
        return 2;
    }
    const char* levelPath = needsInputs ? argv[base] : nullptr;
    const char* geomPath = needsInputs ? argv[base + 1] : nullptr;

    if (selfTest)
        return RunSelfTest(levelPath, geomPath);

    std::vector<uint8_t> levelBytes;
    std::vector<uint8_t> geomBytes;
    std::string reason;
    if (!ReadFileBytes(levelPath, levelBytes, 16ull * 1024ull * 1024ull,
                       reason) ||
        !ReadFileBytes(geomPath, geomBytes, 128ull * 1024ull * 1024ull,
                       reason))
    {
        std::printf("INPUT_READ=FAIL reason='%s'\n", reason.c_str());
        return 1;
    }
    const HashResult levelHash =
        Sha256Hex(levelBytes.data(), levelBytes.size());
    const HashResult geomHash =
        Sha256Hex(geomBytes.data(), geomBytes.size());
    if (!levelHash.ok || !geomHash.ok)
    {
        std::printf("INPUT_SHA256_STATUS=FAIL level='%s' geom='%s'\n",
                    levelHash.ok ? levelHash.hex.c_str()
                                 : levelHash.error.c_str(),
                    geomHash.ok ? geomHash.hex.c_str()
                                : geomHash.error.c_str());
        return 1;
    }
    std::printf("INPUT level_size=%zu level_sha256=%s\n", levelBytes.size(),
                levelHash.hex.c_str());
    std::printf("INPUT geom_size=%zu geom_sha256=%s\n", geomBytes.size(),
                geomHash.hex.c_str());

    const xraylevel12::Limits limits{};
    const uint32_t visuals[] = {9, 105, 106, 107};
    const Fault fault =
        negativeDeterminism ? Fault::DeterminismFailure : Fault::None;
    int failures = 0;
    for (uint32_t visual : visuals)
    {
        const VisualResult result =
            AnalyzeVisual(visual, levelBytes, geomBytes, limits, fault);
        if (result.readError != xraylevel12::ReadError::None ||
            !result.converted || !result.hashOk
#ifndef OGSR_J2B_M0_MUTATE_NO_DETERMINISM_CHECK
            || !result.deterministic
#endif
        )
            ++failures;
    }

    std::printf("M0_REAL_VISUALS_RESULT %s failures=%d\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
