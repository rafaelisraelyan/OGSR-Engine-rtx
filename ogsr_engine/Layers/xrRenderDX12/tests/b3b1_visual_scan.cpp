// F4.0-B3-B1: offline CPU scan of level visuals for a real-camera replay.
//
// Reads the level visual container to enumerate visual ids, reads each visual
// through the existing B1 reader, and reports the projected screen area for a
// captured camera. Test-only tool: it never creates a GPU device.

#include "b3b1_sha256.h"
#include "dx12cameracapturefile.h"
#include "dx12gamecamera.h"
#include "dx12xraylevelgeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
struct VisualHit
{
    uint32_t id = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t stride = 0;
    uint32_t positionOffset = 0;
    double areaPx = 0.0;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    double minZ = 1.0;
    double maxZ = 0.0;
    double distance = 0.0;
    uint32_t insideCount = 0;
    bool fullyInside = false;
};

uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<uint32_t> EnumerateVisualIds(const std::vector<uint8_t>& level)
{
    std::vector<uint32_t> ids;
    size_t position = 0;
    size_t visualsPayload = 0;
    size_t visualsSize = 0;
    while (position + 8 <= level.size())
    {
        const uint32_t type = ReadU32(level, position);
        const uint32_t size = ReadU32(level, position + 4);
        if (size > level.size() - (position + 8))
            break;
        if (type == 3 && (type & 0x80000000u) == 0)
        {
            visualsPayload = position + 8;
            visualsSize = size;
            break;
        }
        position += 8 + size;
    }
    if (visualsSize == 0)
        return ids;

    size_t cursor = visualsPayload;
    const size_t end = visualsPayload + visualsSize;
    while (cursor + 8 <= end)
    {
        const uint32_t id = ReadU32(level, cursor);
        const uint32_t size = ReadU32(level, cursor + 4);
        if (size > end - (cursor + 8))
            break;
        if ((id & 0x80000000u) == 0)
            ids.push_back(id);
        cursor += 8 + size;
    }
    return ids;
}

bool PositionOffset(const xraylevel12::Geometry& geometry, uint32_t& offset)
{
    for (uint32_t i = 0; i < geometry.declaration_count; ++i)
    {
        const auto& element = geometry.declaration[i];
        if (element.usage == 0 && element.usage_index == 0 &&
            element.stream == 0 && element.method == 0 && element.type == 2)
        {
            offset = element.offset;
            return true;
        }
    }
    return false;
}

VisualHit ProjectVisual(const xraylevel12::Geometry& geometry, uint32_t id,
                        const DirectX::XMFLOAT4X4& viewProjection,
                        double cameraX, double cameraY, double cameraZ,
                        uint32_t width, uint32_t height, bool& any)
{
    VisualHit hit;
    hit.id = id;
    hit.vertexCount = geometry.vertex_count;
    hit.indexCount = geometry.index_count;
    hit.stride = geometry.vertex_stride;
    if (!PositionOffset(geometry, hit.positionOffset))
        return hit;

    double minX = 1.0e30, minY = 1.0e30, maxX = -1.0e30, maxY = -1.0e30;
    double minZ = 1.0e30, maxZ = -1.0e30;
    uint32_t inside = 0;
    double maxDistance = 0.0;
    for (uint32_t i = 0; i < geometry.vertex_count; ++i)
    {
        float raw[3] = {};
        const uint64_t base = static_cast<uint64_t>(i) * geometry.vertex_stride +
                              hit.positionOffset;
        if (base + sizeof(raw) > geometry.vertex_bytes.size())
            continue;
        std::memcpy(raw, geometry.vertex_bytes.data() + base, sizeof(raw));
        const DirectX::XMVECTOR input =
            DirectX::XMVectorSet(raw[0], raw[1], raw[2], 1.0f);
        DirectX::XMFLOAT4 clip{};
        DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(
                                          input,
                                          DirectX::XMLoadFloat4x4(&viewProjection)));
        if (!(clip.w > 0.0f))
            continue;
        const double ndcX = clip.x / clip.w;
        const double ndcY = clip.y / clip.w;
        const double ndcZ = clip.z / clip.w;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || !std::isfinite(ndcZ))
            continue;
        if (ndcX < -1.0 || ndcX > 1.0 || ndcY < -1.0 || ndcY > 1.0 ||
            ndcZ < 0.0 || ndcZ > 1.0)
            continue;
        ++inside;
        minX = std::min(minX, ndcX);
        maxX = std::max(maxX, ndcX);
        minY = std::min(minY, ndcY);
        maxY = std::max(maxY, ndcY);
        minZ = std::min(minZ, ndcZ);
        maxZ = std::max(maxZ, ndcZ);
        const double dx = raw[0] - cameraX;
        const double dy = raw[1] - cameraY;
        const double dz = raw[2] - cameraZ;
        maxDistance = std::max(maxDistance, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    any = inside != 0;
    hit.insideCount = inside;
    hit.fullyInside = inside == geometry.vertex_count;
    if (!any)
        return hit;
    const double pixelWidth = (maxX - minX) * 0.5 * width;
    const double pixelHeight = (maxY - minY) * 0.5 * height;
    hit.areaPx = pixelWidth * pixelHeight;
    hit.minX = (minX * 0.5 + 0.5) * width;
    hit.maxX = (maxX * 0.5 + 0.5) * width;
    hit.minY = (0.5 - maxY * 0.5) * height;
    hit.maxY = (0.5 - minY * 0.5) * height;
    hit.minZ = minZ;
    hit.maxZ = maxZ;
    hit.distance = maxDistance;
    return hit;
}
} // namespace

int main(int argc, char** argv)
{
    std::string levelPath;
    std::string geomPath;
    std::string cameraPath;
    std::string csvPath;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) {
            if (i + 1 < argc)
                value = argv[++i];
        };
        if (arg == "--level")
            next(levelPath);
        else if (arg == "--geom")
            next(geomPath);
        else if (arg == "--camera-file")
            next(cameraPath);
        else if (arg == "--csv")
            next(csvPath);
        else
        {
            std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
            return 2;
        }
    }
    if (levelPath.empty() || geomPath.empty() || cameraPath.empty())
    {
        std::fprintf(stderr, "--level, --geom and --camera-file are required\n");
        return 2;
    }

    std::ifstream levelFile(levelPath, std::ios::binary | std::ios::ate);
    std::ifstream geomFile(geomPath, std::ios::binary | std::ios::ate);
    std::ifstream cameraFile(cameraPath, std::ios::binary | std::ios::ate);
    if (!levelFile || !geomFile || !cameraFile)
    {
        std::fprintf(stderr, "cannot open inputs\n");
        return 3;
    }
    std::vector<uint8_t> level(static_cast<size_t>(levelFile.tellg()));
    std::vector<uint8_t> geom(static_cast<size_t>(geomFile.tellg()));
    std::vector<uint8_t> cameraBytes(static_cast<size_t>(cameraFile.tellg()));
    levelFile.seekg(0);
    geomFile.seekg(0);
    cameraFile.seekg(0);
    levelFile.read(reinterpret_cast<char*>(level.data()), level.size());
    geomFile.read(reinterpret_cast<char*>(geom.data()), geom.size());
    cameraFile.read(reinterpret_cast<char*>(cameraBytes.data()), cameraBytes.size());

    cameracapture12::CameraCaptureFile12 capture{};
    cameracapture12::FileError fileError = cameracapture12::FileError::None;
    if (!cameracapture12::Decode(cameraBytes, capture, fileError))
    {
        std::fprintf(stderr, "camera decode failed: %s\n",
                     cameracapture12::FileErrorMessage(fileError));
        return 3;
    }
    const gamecamera12::CameraCaptureResult12 validated =
        gamecamera12::ValidateCamera12(capture.input);
    if (validated.error != gamecamera12::CameraError12::None || !validated.camera)
    {
        std::fprintf(stderr, "camera validation failed: %d\n",
                     static_cast<int>(validated.error));
        return 3;
    }
    const DirectX::XMFLOAT4X4& viewProjection = validated.camera->ViewProjection();
    const DirectX::XMFLOAT3& position = validated.camera->Position();

    const std::vector<uint32_t> ids = EnumerateVisualIds(level);
    std::printf("visuals=%zu camera_sha256=%s viewport=%ux%u position=%.3f,%.3f,%.3f\n",
                ids.size(), b3b1::Sha256Hex(cameraBytes).c_str(),
                validated.camera->ViewportWidth(),
                validated.camera->ViewportHeight(), position.x, position.y,
                position.z);

    std::vector<VisualHit> hits;
    for (uint32_t id : ids)
    {
        xraylevel12::Geometry geometry;
        std::string reason;
        const xraylevel12::ReadError error =
            xraylevel12::ReadVisual(std::span<const uint8_t>(level.data(), level.size()),
                                    std::span<const uint8_t>(geom.data(), geom.size()),
                                    id, geometry, reason);
        if (error != xraylevel12::ReadError::None)
            continue;
        bool any = false;
        const VisualHit hit = ProjectVisual(
            geometry, id, viewProjection, position.x, position.y, position.z,
            validated.camera->ViewportWidth(), validated.camera->ViewportHeight(),
            any);
        if (any)
            hits.push_back(hit);
    }

    std::sort(hits.begin(), hits.end(), [](const VisualHit& a, const VisualHit& b) {
        return a.areaPx > b.areaPx;
    });

    std::printf("visible_visuals=%zu\n", hits.size());
    for (size_t i = 0; i < hits.size() && i < 15; ++i)
    {
        const VisualHit& hit = hits[i];
        std::printf(
            "id=%u verts=%u idx=%u stride=%u posOffset=%u inside=%u/%u "
            "fully=%d area=%.1f px bbox=%.0f,%.0f..%.0f,%.0f depth=%.4f..%.4f "
            "maxdist=%.1f\n",
            hit.id, hit.vertexCount, hit.indexCount, hit.stride,
            hit.positionOffset, hit.insideCount, hit.vertexCount,
            hit.fullyInside ? 1 : 0, hit.areaPx, hit.minX, hit.minY, hit.maxX,
            hit.maxY, hit.minZ, hit.maxZ, hit.distance);
    }

    std::vector<VisualHit> fullyInside;
    for (const VisualHit& hit : hits)
        if (hit.fullyInside)
            fullyInside.push_back(hit);
    std::sort(fullyInside.begin(), fullyInside.end(),
              [](const VisualHit& a, const VisualHit& b) {
                  return a.areaPx > b.areaPx;
              });
    std::printf("fully_inside_visuals=%zu\n", fullyInside.size());
    for (size_t i = 0; i < fullyInside.size() && i < 10; ++i)
    {
        const VisualHit& hit = fullyInside[i];
        std::printf(
            "fully id=%u verts=%u idx=%u area=%.1f px bbox=%.0f,%.0f..%.0f,%.0f "
            "depth=%.4f..%.4f maxdist=%.1f\n",
            hit.id, hit.vertexCount, hit.indexCount, hit.areaPx, hit.minX,
            hit.minY, hit.maxX, hit.maxY, hit.minZ, hit.maxZ, hit.distance);
    }

    if (!csvPath.empty())
    {
        std::ofstream csv(csvPath, std::ios::binary);
        csv << "visual_id,vertex_count,index_count,stride,position_offset,"
               "inside_count,fully_inside,area_px,min_x,min_y,max_x,max_y,"
               "min_z,max_z,max_distance\n";
        for (const VisualHit& hit : hits)
        {
            csv << hit.id << ',' << hit.vertexCount << ',' << hit.indexCount << ','
                << hit.stride << ',' << hit.positionOffset << ',' << hit.insideCount
                << ',' << (hit.fullyInside ? 1 : 0) << ',' << hit.areaPx << ','
                << hit.minX << ',' << hit.minY << ',' << hit.maxX << ',' << hit.maxY
                << ',' << hit.minZ << ',' << hit.maxZ << ',' << hit.distance << '\n';
        }
    }
    return hits.empty() ? 4 : 0;
}
