#include "stdafx.h"
#include "GameDx12GameVisuals.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12RigidModel.h"
#include "fmesh.h"
#include "igame_level.h"
#include "vis_common.h"
#include "xr_object.h"
#include "../Include/xrRender/Kinematics.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gameaux12
{
namespace
{
constexpr size_t kMaxAssets = 2048;
constexpr uint32_t kMaxReadsPerFrame = 96;
constexpr uint64_t kMaxBatchBytes = 48ull * 1024 * 1024;

bool Enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr || std::strcmp(value, "0") != 0;
}

bool Finite(float value)
{
    return (std::bit_cast<uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
}

bool CopyTransform(const Fmatrix& source, DirectX::XMFLOAT4X4& destination)
{
    for (const auto& row : source.m)
        for (const float value : row)
            if (!Finite(value))
                return false;
    static_assert(sizeof(source.m) == sizeof(destination));
    std::memcpy(&destination, source.m, sizeof(destination));
    return true;
}

uint64_t Hash(std::span<const DirectX::XMFLOAT4X4> matrices)
{
    uint64_t result = 14695981039346656037ull;
    for (const std::byte value : std::as_bytes(matrices))
    {
        result ^= std::to_integer<uint8_t>(value);
        result *= 1099511628211ull;
    }
    return result;
}
}

bool GameVisuals::LoadPending(IGame_Level& level, GameDx12StaticScene12& scene,
                              const GameDx12StaticSceneOwner12& owner, std::string& reason)
{
    struct Pending
    {
        Model* model;
        std::vector<Mesh> meshes;
    };
    std::vector<Pending> pending;
    uint64_t batchBytes = 0;
    const bool loadDetails = !m_detailsAttempted && Enabled("OGSR_DX12_GAME_DETAILS");
    if (loadDetails)
    {
        m_detailsAttempted = true;
        std::string detailReason;
        m_detailsLoaded = m_details.Load(detailReason);
        Msg("ph_dx12_f42: details_load status=%s reason='%s' models=%zu",
            m_detailsLoaded ? "loaded" : "skipped", detailReason.c_str(), m_details.Models().size());
    }

    const uint32_t count = level.Objects.o_count();
    if (Enabled("OGSR_DX12_GAME_RIGID"))
    {
        uint32_t reads = 0;
        for (uint32_t i = 0; i < count && reads < kMaxReadsPerFrame; ++i)
        {
            const auto* object = level.Objects.o_get_by_iterator(i);
            if (!object || object->getDestroy() || !object->getVisible() ||
                !object->Visual() || object->Visual()->getType() != MT_SKELETON_RIGID)
                continue;
            const shared_str visual = object->cNameVisual();
            if (!*visual || m_models.contains(visual.c_str()))
                continue;
            if (m_models.size() >= kMaxAssets || batchBytes >= kMaxBatchBytes)
                break;
            ++reads;
            std::vector<Mesh> meshes;
            std::string rejected;
            const bool loaded = ReadRigidModel(visual.c_str(), meshes, rejected);
            auto& model = m_models.try_emplace(visual.c_str()).first->second;
            if (!loaded)
            {
                model.reason = std::move(rejected);
                if (m_modelWarnings < 36)
                {
                    ++m_modelWarnings;
                    Msg("ph_dx12_f42: rigid_asset skipped visual='%s' reason='%s'",
                        visual.c_str(), model.reason.c_str());
                }
                continue;
            }
            for (const auto& mesh : meshes)
                batchBytes += mesh.vertices.size() * sizeof(Vertex) +
                    mesh.indices.size() * sizeof(uint16_t);
            pending.push_back({&model, std::move(meshes)});
        }
    }

    std::vector<const Mesh*> sources;
    if (loadDetails && m_detailsLoaded)
        for (const auto& model : m_details.Models())
            sources.push_back(&model);
    const size_t detailCount = sources.size();
    for (const auto& entry : pending)
        for (const auto& mesh : entry.meshes)
            sources.push_back(&mesh);
    if (sources.empty())
        return true;

    std::vector<uint32_t> handles;
    if (!scene.RegisterMeshes(owner, sources, handles, reason))
        return false;
    for (size_t index = 0; index < detailCount; ++index)
        m_frame.details.push_back({handles[index], {}});
    size_t next = detailCount;
    for (const auto& entry : pending)
    {
        auto& model = *entry.model;
        bool published = false;
        for (const auto& mesh : entry.meshes)
        {
            const uint32_t handle = handles[next++];
            uint64_t boneMask = 0;
            for (const uint16_t vertex : mesh.indices)
                boneMask |= uint64_t{1} << mesh.vertices[vertex].bone;
            model.parts.push_back({handle, boneMask});
            published |= handle != kInvalidMesh;
        }
        if (published)
            ++m_counts.newModels;
        else
            model.reason = "mesh_or_material_unavailable";
    }
    return true;
}

void GameVisuals::Capture(IGame_Level& level, const float eye[3])
{
    m_counts.objects = level.Objects.o_count();
    if (!Enabled("OGSR_DX12_GAME_RIGID"))
        return;
    for (uint32_t i = 0; i < m_counts.objects; ++i)
    {
        CObject* object = level.Objects.o_get_by_iterator(i);
        IRenderVisual* visual = object ? object->Visual() : nullptr;
        if (!visual)
            continue;
        ++m_counts.visuals;
        if (visual->getType() != MT_SKELETON_RIGID)
        {
            ++m_counts.unsupported;
            continue;
        }
        ++m_counts.rigid;
        if (object->getDestroy() || object->GetTmpPreDestroy())
        {
            ++m_counts.destroyed;
            continue;
        }
        if (!object->getVisible())
        {
            ++m_counts.hidden;
            continue;
        }
        const shared_str visualName = object->cNameVisual();
        if (!*visualName)
        {
            ++m_counts.missingModel;
            ++m_rejections["missing_visual_name"];
            continue;
        }
        const auto model = m_models.find(visualName.c_str());
        if (model == m_models.end())
        {
            ++m_counts.waitingModel;
            continue;
        }
        if (!model->second.reason.empty())
        {
            ++m_counts.missingModel;
            ++m_rejections[model->second.reason];
            continue;
        }
        ++m_counts.reused;

        auto* skeleton = visual->dcast_PKinematics();
        const uint32_t bones = skeleton ? skeleton->LL_BoneCount() : 0;
        if (!bones || bones > kMaxBones || !object->Dx12Generation())
        {
            ++m_counts.invalidPose;
            continue;
        }
        if (bones > kMaxFrameTransforms - m_frame.poses.size())
        {
            ++m_counts.poseBudget;
            continue;
        }
        const auto validMask = bones == kMaxBones ? UINT64_MAX : (uint64_t{1} << bones) - 1;
        skeleton->CalculateBones(TRUE);
        const uint64_t visibleChildren = skeleton->RigidVisibleChildMask();
        ObjectDraw entry;
        entry.id = object->ID();
        entry.generation = object->Dx12Generation();
        entry.poseOffset = static_cast<uint32_t>(m_frame.poses.size());
        entry.firstMesh = static_cast<uint32_t>(m_frame.objectMeshes.size());
        Fvector center;
        object->Center(center);
        entry.center[0] = center.x;
        entry.center[1] = center.y;
        entry.center[2] = center.z;
        entry.radius = object->Radius();
        bool valid = CopyTransform(object->XFORM(), entry.world) &&
            Finite(center.x) && Finite(center.y) && Finite(center.z) &&
            Finite(entry.radius) && entry.radius >= 0.0f;
        for (uint16_t bone = 0; valid && bone < bones; ++bone)
        {
            // inverseBind * pose * world с учётом row-vector соглашения X-Ray.
            Fmatrix pose;
            pose.mul_43(object->XFORM(), skeleton->LL_GetTransform_R(bone));
            DirectX::XMFLOAT4X4 composed;
            valid = CopyTransform(pose, composed);
            if (valid)
                m_frame.poses.push_back(composed);
        }
        if (!valid)
        {
            m_frame.poses.resize(entry.poseOffset);
            ++m_counts.invalidPose;
            continue;
        }
        ++m_counts.validPose;

        const auto& parts = model->second.parts;
        for (size_t part = 0; part < parts.size(); ++part)
        {
            if (!(visibleChildren & (uint64_t{1} << part)))
                continue;
            if (parts[part].mesh == kInvalidMesh || (parts[part].boneMask & ~validMask))
            {
                ++m_counts.missingParts;
                continue;
            }
            m_frame.objectMeshes.push_back(parts[part].mesh);
            ++entry.meshCount;
        }
        if (entry.meshCount == 0)
        {
            m_frame.poses.resize(entry.poseOffset);
            ++m_counts.childHidden;
            continue;
        }
        const float distance = std::hypot(center.x - eye[0], center.y - eye[1], center.z - eye[2]);
        if (distance < CROW_RADIUS2 + entry.radius)
            object->MakeMeCrow();
        m_frame.objects.push_back(entry);
    }
}

bool GameVisuals::Prepare(IGame_Level& level, const float eye[3], GameDx12StaticScene12& scene,
                          const GameDx12StaticSceneOwner12& owner, std::string& reason)
{
    if (m_frame.session != scene.LevelSession() || m_frame.epoch != scene.LevelEpoch())
        Reset();
    m_frame.session = scene.LevelSession();
    m_frame.epoch = scene.LevelEpoch();
    m_frame.objects.clear();
    m_frame.objectMeshes.clear();
    m_frame.poses.clear();
    m_counts = {};
    m_rejections.clear();
    if (!LoadPending(level, scene, owner, reason))
        return false;
    Capture(level, eye);
    if (m_detailsLoaded)
    {
        m_details.Collect(eye, m_detailInstances, m_detailStats);
        for (size_t model = 0; model < m_frame.details.size(); ++model)
            m_frame.details[model].transforms.swap(m_detailInstances[model]);
    }
    return true;
}

void GameVisuals::Reset()
{
    *this = GameVisuals{};
}

void GameVisuals::LogPresent(uint64_t present, uint64_t fence,
    const GameDx12StaticScene12::AuxStats& draws, const GameDx12StaticScene12::AuxStats& resources) const
{
    if (present <= 480)
    {
        static const char* value = std::getenv("OGSR_DX12_GAME_F42_TRACE_ID");
        if (value && *value)
        {
            char* end = nullptr;
            const auto id = std::strtoul(value, &end, 10);
            if (end != value && *end == '\0' && id <= 65535)
            {
                const auto it = std::find_if(m_frame.objects.begin(), m_frame.objects.end(),
                    [id](const ObjectDraw& entry) { return entry.id == id; });
                if (it == m_frame.objects.end())
                    Msg("ph_dx12_f42: trace present=%llu fence=%llu session=%llu id=%lu state=absent",
                        present, fence, m_frame.session, id);
                else
                {
                    const auto next = std::next(it);
                    const auto endPose = next != m_frame.objects.end() ?
                        next->poseOffset : m_frame.poses.size();
                    Msg("ph_dx12_f42: trace present=%llu fence=%llu session=%llu id=%lu "
                        "generation=%llu state=prepared parts=%u world_hash=%016llx pose_hash=%016llx "
                        "world=(%.4f,%.4f,%.4f)", present, fence, m_frame.session, id,
                        it->generation, it->meshCount, Hash(std::span(&it->world, 1)),
                        Hash(std::span(m_frame.poses).subspan(it->poseOffset, endPose - it->poseOffset)),
                        it->world._41, it->world._42, it->world._43);
                }
            }
        }
    }
    if (present != 1 && present != 30 && present % 120 != 0)
        return;
    Msg("ph_dx12_f42: rigid present=%llu fence=%llu objects=%u visuals=%u supported_type=%u "
        "unsupported_type=%u destroyed=%u hidden=%u valid_pose=%u invalid_pose=%u child_hidden=%u "
        "waiting_model=%u rejected_model=%u missing_parts=%u pose_budget=%u "
        "visible=%u culled=%u submitted=%u draws=%u new_models=%u reused_objects=%u",
        present, fence, m_counts.objects, m_counts.visuals, m_counts.rigid, m_counts.unsupported,
        m_counts.destroyed, m_counts.hidden, m_counts.validPose, m_counts.invalidPose,
        m_counts.childHidden, m_counts.waitingModel, m_counts.missingModel, m_counts.missingParts,
        m_counts.poseBudget, draws.rigidVisible, draws.rigidCulled, draws.rigidSubmitted,
        draws.rigidDraws, m_counts.newModels, m_counts.reused);
    for (const auto& [reason, count] : m_rejections)
        Msg("ph_dx12_f42: rigid_skip present=%llu reason='%s' objects=%u", present, reason.c_str(), count);
    Msg("ph_dx12_f42: resources meshes=%u geometry_bytes=%llu new_textures=%u texture_bytes=%llu",
        resources.meshesUploaded, resources.geometryBytes, resources.texturesUploaded, resources.textureBytes);
    if (!m_detailsLoaded)
        return;
    const auto& s = m_detailStats;
    Msg("ph_dx12_f42: details present=%llu models=%u slots=%u occupied_slots=%u "
        "geometry_bytes=%llu source=%u eligible=%u visible=%u submitted=%u draws=%u "
        "skipped_surface=%u skipped_slope=%u skipped_near=%u skipped_distance=%u skipped_lod=%u "
        "skipped_frustum=%u skipped_material=%u skipped_budget=%u pending_slots=%u "
        "budget_slots=%u invalid_entries=%u invalid_models=%u",
        present, s.models, s.slots, s.occupiedSlots, s.geometryBytes, s.source, s.eligible,
        draws.detailVisible, draws.detailSubmitted, draws.detailDraws, s.noSurface, s.slopeRejected,
        s.nearExcluded, s.distanceCulled, s.lodCulled, draws.detailCulled, draws.detailMissingMaterial,
        draws.detailBudget + s.candidateBudget, s.pendingSlots, s.cacheBudgetSlots,
        s.invalidEntries, s.invalidModels);
}
}

#endif
