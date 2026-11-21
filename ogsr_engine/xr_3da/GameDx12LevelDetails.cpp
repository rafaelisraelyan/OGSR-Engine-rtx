#include "stdafx.h"
#include "GameDx12LevelDetails.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameMtlLib.h"
#include "IGame_Level.h"
#include "LocatorAPI_defs.h"
#include "Render.h"
#include "dx12scopeguard.h"
#include "../Layers/xrRender/DetailFormat.h"
#include "../Layers/xrRender/xrRender_console.h"
#include "../xrCDB/cl_intersect.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <string_view>

extern float dm_current_fade;
extern float ps_current_detail_density;
extern float ps_current_detail_scale;
extern BOOL ps_no_scale_on_fade;

namespace gameaux12
{
namespace
{
using Bytes = std::span<const uint8_t>;

constexpr size_t kMaxFileBytes = 128u * 1024u * 1024u;
constexpr size_t kMaxGeometryBytes = 32u * 1024u * 1024u;
constexpr uint32_t kMaxSlots = 4u * 1024u * 1024u;
constexpr uint32_t kMaxModels = 64;
constexpr int32_t kMaxSlotCoordinate = 1 << 19;
constexpr uint32_t kMaxSlotsPerFrame = 256;
constexpr size_t kMaxCachedPlacements = 1024u * 1024u;
constexpr size_t kMaxFramePlacements = kMaxFrameTransforms;
constexpr uint32_t kPlacementGridLimit = 11;
constexpr size_t kMaxPlacementsPerSlot = (kPlacementGridLimit + 1) * (kPlacementGridLimit + 1);
constexpr float kMaxFade = 300.f;

static_assert(sizeof(DetailHeader) == 24);
static_assert(sizeof(DetailSlot) == 16);

struct DiskVertex
{
    float position[3];
    float uv[2];
};
static_assert(sizeof(DiskVertex) == 20);

template<class Value>
bool Read(Bytes& bytes, Value& value)
{
    if (bytes.size() < sizeof(value))
        return false;
    std::memcpy(&value, bytes.data(), sizeof(value));
    bytes = bytes.subspan(sizeof(value));
    return true;
}

bool ReadString(Bytes& bytes, std::string_view& value)
{
    const size_t limit = (std::min)(bytes.size(), size_t{256});
    if (limit == 0)
        return false;
    const auto* end = static_cast<const uint8_t*>(std::memchr(bytes.data(), 0, limit));
    if (!end || end == bytes.data())
        return false;
    const auto size = static_cast<size_t>(end - bytes.data());
    value = std::string_view(reinterpret_cast<const char*>(bytes.data()), size);
    bytes = bytes.subspan(size + 1);
    return true;
}

bool ReadChunks(Bytes bytes, std::span<Bytes> chunks, std::string& reason)
{
    std::array<bool, kMaxModels> seen{};
    while (!bytes.empty())
    {
        uint32_t id = 0;
        uint32_t size = 0;
        if (!Read(bytes, id) || !Read(bytes, size) || size > bytes.size())
        {
            reason = "truncated detail chunk";
            return false;
        }
        if (id & CFS_CompressMark)
        {
            reason = "compressed detail chunk unsupported";
            return false;
        }
        if (id >= chunks.size() || seen[id])
        {
            reason = "unexpected or duplicate detail chunk";
            return false;
        }
        seen[id] = true;
        chunks[id] = bytes.first(size);
        bytes = bytes.subspan(size);
    }
    if (std::find(seen.begin(), seen.begin() + chunks.size(), false) != seen.begin() + chunks.size())
    {
        reason = "missing detail chunk";
        return false;
    }
    return true;
}

bool SetTexture(std::string_view source, std::string& texture)
{
    source = source.substr(0, source.find(','));
    texture.assign(source);
    for (char& letter : texture)
    {
        if (letter >= 'A' && letter <= 'Z')
            letter = static_cast<char>(letter + ('a' - 'A'));
        if (letter == '/')
            letter = '\\';
    }
    if (texture.ends_with(".dds"))
        texture.resize(texture.size() - 4);
    return !texture.empty() && texture.size() <= 180 && texture.front() != '\\' &&
        texture.find("..") == std::string::npos &&
        texture.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-.\\") == std::string::npos;
}

bool ParseModel(Bytes bytes, Mesh& mesh, float& minScale, float& maxScale, float& radius,
                uint64_t& geometryBytes)
{
    std::string_view shader, texture;
    uint32_t flags = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    if (!ReadString(bytes, shader) || !ReadString(bytes, texture) || !Read(bytes, flags) ||
        !Read(bytes, minScale) || !Read(bytes, maxScale) || !Read(bytes, vertexCount) ||
        !Read(bytes, indexCount) || !std::isfinite(minScale) || !std::isfinite(maxScale) ||
        minScale <= 0.f || minScale > maxScale || maxScale > 64.f ||
        vertexCount < 3 || vertexCount > 65536 || indexCount < 3 || (indexCount % 3) != 0)
        return false;

    const uint64_t indexBytes = uint64_t{indexCount} * sizeof(uint16_t);
    const uint64_t inputBytes = uint64_t{vertexCount} * sizeof(DiskVertex) + indexBytes;
    const uint64_t outputBytes = uint64_t{vertexCount} * sizeof(Vertex) + indexBytes;
    if (inputBytes != bytes.size() || outputBytes > kMaxGeometryBytes - geometryBytes)
        return false;

    Mesh model;
    if (!SetTexture(texture, model.texture))
        return false;
    model.cutout = true;
    model.vertices.reserve(vertexCount);
    Fbox bounds;
    bounds.invalidate();
    for (uint32_t index = 0; index < vertexCount; ++index)
    {
        DiskVertex input{};
        if (!Read(bytes, input))
            return false;
        const auto valid = [](float value) { return std::isfinite(value) && std::abs(value) <= 10000.f; };
        if (!std::all_of(std::begin(input.position), std::end(input.position), valid) ||
            !std::all_of(std::begin(input.uv), std::end(input.uv), valid))
            return false;
        bounds.modify(Fvector{input.position[0], input.position[1], input.position[2]});
        // Для освещения травы R4 использует нормаль поверхности. При наличии
        // выравнивания матрица инстанса переводит эту ось в нормаль CForm.
        model.vertices.push_back({{input.position[0], input.position[1], input.position[2]},
                                  {0.f, 1.f, 0.f}, {input.uv[0], input.uv[1]}});
    }
    model.indices.reserve(indexCount);
    for (uint32_t index = 0; index < indexCount; ++index)
    {
        uint16_t vertex = 0;
        if (!Read(bytes, vertex) || vertex >= vertexCount)
            return false;
        model.indices.push_back(vertex);
    }

    Fvector center;
    bounds.getsphere(center, radius);
    if (!std::isfinite(radius) || radius <= 0.f)
        return false;
    geometryBytes += outputBytes;
    mesh = std::move(model);
    return true;
}

constexpr auto MakeDitherTable()
{
    constexpr int magic[4][4]{{0, 14, 3, 13}, {11, 5, 8, 6}, {12, 2, 15, 1}, {7, 9, 4, 10}};
    constexpr float factor = (255.f - 1.f) / 16.f;
    std::array<std::array<int, 16>, 16> table{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                for (int l = 0; l < 4; ++l)
                    table[4 * k + i][4 * l + j] =
                        static_cast<int>(0.5 + magic[i][j] * factor + (magic[k][l] / 16.) * factor);
    return table;
}

bool PassPalette(uint16_t palette, uint32_t x, uint32_t z, uint32_t shiftX,
                 uint32_t shiftZ, uint32_t gridSize)
{
    static constexpr auto dither = MakeDitherTable();
    x = (std::min)(x, gridSize - 1);
    z = (std::min)(z, gridSize - 1);
    const float fx = static_cast<float>(x) / static_cast<float>(gridSize);
    const float fz = static_cast<float>(z) / static_cast<float>(gridSize);
    const float a0 = 17.f * (palette & 15);
    const float a1 = 17.f * ((palette >> 4) & 15);
    const float a2 = 17.f * ((palette >> 8) & 15);
    const float a3 = 17.f * ((palette >> 12) & 15);
    const float low = a0 * (1.f - fx) + a1 * fx;
    const float high = a2 * (1.f - fx) + a3 * fx;
    const float left = a0 * (1.f - fz) + a2 * fz;
    const float right = a1 * (1.f - fz) + a3 * fz;
    const float acrossX = low * (1.f - fz) + high * fz;
    const float acrossZ = left * (1.f - fx) + right * fx;
    const int alpha = static_cast<int>((acrossX + acrossZ) * 0.5f + 0.5f);
    return alpha > dither[(x + shiftX) % 16][(z + shiftZ) % 16];
}

float ReadSetting(float value, float lower, float upper, float fallback)
{
    return std::isfinite(value) ? std::clamp(value, lower, upper) : fallback;
}
} // namespace

bool LevelDetails::Load(std::string& reason)
{
    *this = LevelDetails{};
    reason.clear();
    try
    {
        IReader* file = FS.r_open(fsgame::level, fsgame::level_files::level_details);
        if (!file)
        {
            reason = "level.details missing";
            return false;
        }
        auto close = dx12scope::MakeScopeGuard([&file]() noexcept { FS.r_close(file); });
        const size_t size = file->length();
        if (size == 0 || size > kMaxFileBytes)
        {
            reason = "level.details file size rejected";
            return false;
        }
        LevelDetails scene;
        if (!scene.Parse(Bytes(static_cast<const uint8_t*>(file->begin()), size), reason))
            return false;
        *this = std::move(scene);
        return true;
    }
    catch (...)
    {
        reason = "level.details read or allocation failed";
        return false;
    }
}

bool LevelDetails::Parse(Bytes bytes, std::string& reason)
{
    std::array<Bytes, 3> chunks;
    if (!ReadChunks(bytes, chunks, reason))
        return false;

    DetailHeader header{};
    if (!Read(chunks[0], header) || !chunks[0].empty() || header.version != DETAIL_VERSION)
    {
        reason = "invalid level.details v3 header";
        return false;
    }
    const uint64_t slots = uint64_t{header.size_x} * header.size_z;
    const auto validExtent = [](int32_t offset, uint32_t count)
    {
        return offset >= -kMaxSlotCoordinate && offset <= kMaxSlotCoordinate &&
            int64_t{count} - offset <= kMaxSlotCoordinate;
    };
    if (header.object_count > kMaxModels || slots == 0 || slots > kMaxSlots ||
        slots * sizeof(DetailSlot) != chunks[2].size() ||
        !validExtent(header.offs_x, header.size_x) || !validExtent(header.offs_z, header.size_z))
    {
        reason = "invalid level.details grid or model count";
        return false;
    }

    std::array<Bytes, kMaxModels> models;
    if (!ReadChunks(chunks[1], std::span(models).first(header.object_count), reason))
        return false;
    m_loadStats.models = header.object_count;
    m_loadStats.slots = static_cast<uint32_t>(slots);
    m_models.resize(header.object_count);
    m_modelParameters.resize(header.object_count);
    for (uint32_t model = 0; model < header.object_count; ++model)
    {
        auto& p = m_modelParameters[model];
        if (!ParseModel(models[model], m_models[model], p.minScale, p.maxScale, p.radius,
                        m_loadStats.geometryBytes))
            ++m_loadStats.invalidModels;
    }

    m_width = header.size_x;
    m_height = header.size_z;
    m_offsetX = header.offs_x;
    m_offsetZ = header.offs_z;
    m_slots.resize(static_cast<size_t>(slots));
    for (Slot& slot : m_slots)
    {
        DetailSlot source{};
        if (!Read(chunks[2], source))
        {
            reason = "truncated level.details slots";
            return false;
        }
        slot.yBase = static_cast<uint16_t>(source.y_base);
        slot.yHeight = static_cast<uint8_t>(source.y_height);
        std::memcpy(slot.palette.data(), source.palette, sizeof(source.palette));
        for (uint32_t index = 0; index < slot.model.size(); ++index)
        {
            slot.model[index] = source.r_id(index);
            if (slot.model[index] == DetailSlot::ID_Empty)
                continue;
            if (slot.model[index] >= m_models.size() || m_models[slot.model[index]].vertices.empty())
            {
                ++m_loadStats.invalidEntries;
                slot.model[index] = DetailSlot::ID_Empty;
                continue;
            }
            slot.occupied |= slot.palette[index] != 0;
        }
        m_loadStats.occupiedSlots += slot.occupied;
    }
    return true;
}

LevelDetails::CachedSlot LevelDetails::Decompress(uint32_t index) const
{
    CachedSlot output;
    const Slot& slot = m_slots[index];
    const int32_t sx = static_cast<int32_t>(index % m_width) - m_offsetX;
    const int32_t sz = static_cast<int32_t>(index / m_width) - m_offsetZ;
    const float xBase = static_cast<float>(sx) * DETAIL_SLOT_SIZE;
    const float zBase = static_cast<float>(sz) * DETAIL_SLOT_SIZE;
    const float yBase = static_cast<float>(slot.yBase) * 0.2f - 200.f;
    const float yHeight = static_cast<float>(slot.yHeight) * 0.1f;
    constexpr float halfSlot = DETAIL_SLOT_SIZE * 0.5f;
    const Fvector boxCenter{xBase + halfSlot, yBase + yHeight * 0.5f, zBase + halfSlot};
    const Fvector boxExtents{halfSlot + EPS_L, yHeight * 0.5f + EPS_L, halfSlot + EPS_L};
    XRC.box_query(CDB::OPT_FULL_TEST, g_pGameLevel->ObjectSpace.GetStaticModel(), boxCenter, boxExtents);
    CDB::RESULT* const surfaceHits = XRC.r_begin();
    const size_t surfaceCount = XRC.r_count();

    // Один и тот же слот сохраняет исходное распределение при возврате камеры.
    const uint32_t seed = (static_cast<uint32_t>(sx) * 73856093u) ^
                          (static_cast<uint32_t>(sz) * 19349663u) ^ 0x71c4b287u;
    std::mt19937 rng(seed);
    const auto random = [&] { return std::generate_canonical<float, 24>(rng); };
    const uint32_t grid = (std::min)(kPlacementGridLimit,
        static_cast<uint32_t>(std::ceil(DETAIL_SLOT_SIZE / m_settings.density)));
    const float step = DETAIL_SLOT_SIZE / static_cast<float>(grid);
    const float jitter = m_settings.density / 1.7f;
    const Fvector down{0.f, -1.f, 0.f};
    const uint32_t materialCount = GMLib.CountMaterial();
    for (uint32_t z = 0; z <= grid; ++z)
        for (uint32_t x = 0; x <= grid; ++x)
        {
            const uint32_t shiftX = rng() % 16;
            const uint32_t shiftZ = rng() % 16;
            std::array<uint8_t, 4> selected;
            uint32_t count = 0;
            for (uint32_t group = 0; group < slot.model.size(); ++group)
                if (slot.model[group] != DetailSlot::ID_Empty &&
                    PassPalette(slot.palette[group], x, z, shiftX, shiftZ, grid))
                    selected[count++] = slot.model[group];
            if (count == 0)
                continue;

            const uint8_t model = selected[count == 1 ? 0 : rng() % count];
            Fvector point{xBase - EPS_L + static_cast<float>(x) * step + (2.f * random() - 1.f) * jitter,
                          yBase + yHeight + EPS_L,
                          zBase - EPS_L + static_cast<float>(z) * step + (2.f * random() - 1.f) * jitter};
            float nearest = yHeight + 2.f * EPS_L;
            Fvector up{};
            bool hit = false;
            for (size_t surface = 0; surface < surfaceCount; ++surface)
            {
                CDB::RESULT& triangle = surfaceHits[surface];
                if (triangle.material >= materialCount ||
                    GMLib.GetMaterialByIdx(static_cast<u16>(triangle.material))->Flags.test(SGameMtl::flPassable))
                    continue;
                float u, v, distance;
                if (CDB::TestRayTri(point, down, triangle.verts, u, v, distance, true) &&
                    std::isfinite(distance) && distance >= 0.f && distance <= nearest)
                {
                    nearest = distance;
                    up.mknormal(triangle.verts[0], triangle.verts[1], triangle.verts[2]);
                    hit = true;
                }
            }
            if (!hit)
            {
                ++output.noSurface;
                continue;
            }

            const float slope = m_settings.slope * (0.8f + 0.2f * random());
            if (up.y < 1.f - slope)
            {
                ++output.slopeRejected;
                continue;
            }
            const auto& p = m_modelParameters[model];
            const float scale = std::lerp(p.minScale * 0.5f, p.maxScale * 0.9f, random()) * m_settings.scale;
            point.y -= nearest;
            output.placements.push_back({{point.x, point.y, point.z}, {up.x, up.y, up.z},
                                         random() * PI_MUL_2, scale, model});
        }
    return output;
}

void LevelDetails::Collect(const float eye[3],
                           std::vector<std::vector<DirectX::XMFLOAT4X4>>& instances,
                           DetailStats& stats)
{
    instances.resize(m_models.size());
    for (auto& model : instances)
        model.clear();
    stats = m_loadStats;
    stats.enabled = psDeviceFlags.test(rsDetails);
    if (m_slots.empty() || !stats.enabled || !g_pGameLevel || !std::isfinite(eye[0]) ||
        !std::isfinite(eye[1]) || !std::isfinite(eye[2]) ||
        std::abs(eye[0]) > 2.f * kMaxSlotCoordinate || std::abs(eye[2]) > 2.f * kMaxSlotCoordinate)
        return;

    const PlacementSettings settings{
        ReadSetting(ps_current_detail_density, 0.2f, 0.9f, 0.6f),
        ReadSetting(ps_current_detail_scale, 0.7f, 1.5f, 1.f),
        ReadSetting(ps_ssfx_terrain_grass_slope, 0.f, 1.f, 1.f),
        ps_ssfx_terrain_grass_align != 0};
    if (m_settings != settings)
    {
        m_settings = settings;
        m_cache.clear();
        m_cachedPlacements = 0;
    }

    const float fade = ReadSetting(dm_current_fade, 2.f, kMaxFade, 47.5f);
    const float fadeSquared = fade * fade;
    const float nearRadius = ReadSetting(ps_r2_no_details_radius, 0.f, kMaxFade, 0.f);
    const float nearSquared = nearRadius * nearRadius;
    const int32_t radius = static_cast<int32_t>(std::ceil(fade / DETAIL_SLOT_SIZE)) + 1;
    const int32_t centerX = static_cast<int32_t>(std::floor(eye[0] / DETAIL_SLOT_SIZE + 0.5f)) + m_offsetX;
    const int32_t centerZ = static_cast<int32_t>(std::floor(eye[2] / DETAIL_SLOT_SIZE + 0.5f)) + m_offsetZ;
    const int32_t xMin = (std::max)(0, centerX - radius);
    const int32_t xMax = (std::min)(static_cast<int32_t>(m_width) - 1, centerX + radius);
    const int32_t zMin = (std::max)(0, centerZ - radius);
    const int32_t zMax = (std::min)(static_cast<int32_t>(m_height) - 1, centerZ + radius);

    std::erase_if(m_cache, [&](const auto& entry)
    {
        const int32_t x = static_cast<int32_t>(entry.first % m_width);
        const int32_t z = static_cast<int32_t>(entry.first / m_width);
        const bool outside = x < xMin || x > xMax || z < zMin || z > zMax;
        if (outside)
            m_cachedPlacements -= entry.second.placements.size();
        return outside;
    });

    m_ready.clear();
    m_pending.clear();
    for (int32_t z = zMin; z <= zMax; ++z)
        for (int32_t x = xMin; x <= xMax; ++x)
        {
            const uint32_t index = static_cast<uint32_t>(z) * m_width + static_cast<uint32_t>(x);
            if (!m_slots[index].occupied)
                continue;
            if (m_cache.contains(index))
            {
                m_ready.push_back(index);
                continue;
            }

            const float dx = (static_cast<float>(x - m_offsetX) + 0.5f) * DETAIL_SLOT_SIZE - eye[0];
            const float dz = (static_cast<float>(z - m_offsetZ) + 0.5f) * DETAIL_SLOT_SIZE - eye[2];
            m_pending.push_back({index, dx * dx + dz * dz});
        }

    const size_t toLoad = (std::min)(m_pending.size(), size_t{kMaxSlotsPerFrame});
    if (toLoad != 0)
        std::partial_sort(m_pending.begin(), m_pending.begin() + toLoad, m_pending.end(),
            [](const PendingSlot& lhs, const PendingSlot& rhs)
            {
                if (lhs.distanceSquared != rhs.distanceSquared)
                    return lhs.distanceSquared < rhs.distanceSquared;
                return lhs.index < rhs.index;
            });
    size_t loaded = 0;
    for (; loaded < toLoad; ++loaded)
    {
        if (m_cachedPlacements > kMaxCachedPlacements - kMaxPlacementsPerSlot)
        {
            stats.cacheBudgetSlots = static_cast<uint32_t>(m_pending.size() - loaded);
            break;
        }
        const uint32_t index = m_pending[loaded].index;
        const auto& slot = m_cache.emplace(index, Decompress(index)).first->second;
        m_cachedPlacements += slot.placements.size();
        m_ready.push_back(index);
    }
    stats.pendingSlots = static_cast<uint32_t>(m_pending.size() - loaded);

    // CRender::Calculate выставляет свой r_ssaDISCARD только в R4-кадре.
    const float width = static_cast<float>((std::max)(Device.dwWidth, u32{1}));
    const float height = static_cast<float>((std::max)(Device.dwHeight, u32{1}));
    const float fov = ReadSetting(Device.fFOV, 1.f, 179.f, 90.f);
    const float lod = ReadSetting(ps_r__LOD, EPS_S, 16.f, 1.f);
    const float discard = ReadSetting(ps_r__ssaDISCARD, 0.5f, 10.f, 3.5f);
    const float perspective = 90.f / fov;
    const float ssaDiscard = discard * discard / (width * height * perspective * perspective * (lod + EPS_S));

    size_t emitted = 0;
    for (const uint32_t index : m_ready)
    {
        const auto& slot = m_cache.at(index);
        stats.noSurface += slot.noSurface;
        stats.slopeRejected += slot.slopeRejected;
        stats.eligible += static_cast<uint32_t>(slot.placements.size());
        for (const Placement& item : slot.placements)
        {
            const float x = item.position[0] - eye[0];
            const float y = item.position[1] - eye[1];
            const float z = item.position[2] - eye[2];
            const float distanceSquared = x * x + y * y + z * z;
            if (distanceSquared < nearSquared)
            {
                ++stats.nearExcluded;
                continue;
            }
            if (distanceSquared >= fadeSquared)
            {
                ++stats.distanceCulled;
                continue;
            }

            const float fadeFactor = std::clamp((fadeSquared - distanceSquared) / (fadeSquared - 1.f), 0.f, 1.f);
            const float scale = ps_no_scale_on_fade ? item.scale : item.scale * fadeFactor;
            const float projectedRadius = m_modelParameters[item.model].radius * scale;
            const float ssa = ps_no_scale_on_fade ? scale : projectedRadius * projectedRadius /
                (std::max)(distanceSquared, 1.f);
            if (ssa < ssaDiscard)
            {
                ++stats.lodCulled;
                continue;
            }
            if (emitted == kMaxFramePlacements)
            {
                ++stats.candidateBudget;
                continue;
            }

            Fmatrix rotation;
            rotation.rotateY(item.yaw);
            if (settings.align)
            {
                const Fmatrix yaw = rotation;
                rotation.j.set(item.up[0], item.up[1], item.up[2]);
                Fvector::generate_orthonormal_basis(rotation.j, rotation.i, rotation.k);
                rotation.mulB_43(yaw);
            }
            rotation.i.mul(scale);
            rotation.j.mul(scale);
            rotation.k.mul(scale);
            rotation.translate_over(item.position[0], item.position[1], item.position[2]);
            auto& world = instances[item.model].emplace_back();
            static_assert(sizeof(world.m) == sizeof(rotation.m));
            std::memcpy(world.m, rotation.m, sizeof(world.m));
            ++emitted;
        }
    }
    stats.source = stats.eligible + stats.noSurface + stats.slopeRejected;
}
} // namespace gameaux12

#endif
