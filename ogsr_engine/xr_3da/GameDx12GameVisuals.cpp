#include "stdafx.h"
#include "GameDx12GameVisuals.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12RigidModel.h"
#include "fmesh.h"
#include "igame_level.h"
#include "vis_common.h"
#include "xr_object.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Layers/xrRender/LightTrack.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

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

bool SkinnedEnabled()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_SKINNED");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

shared_str SourceVisualName(const CObject& object, bool skinned)
{
    if (skinned)
    {
        // CModelPool сохраняет нормализованное имя загруженного visual.
        const auto name = object.Visual()->getDebugName();
        if (*name)
            return name;
    }
    return object.cNameVisual();
}

uint32_t RequiredBones(const Mesh& mesh)
{
    uint32_t count = 0;
    for (const uint16_t index : mesh.indices)
    {
        const auto& vertex = mesh.vertices[index];
        for (size_t influence = 0; influence < std::size(vertex.bones); ++influence)
            if (vertex.weights[influence] > 0.0f)
                count = (std::max)(count, vertex.bones[influence] + 1);
    }
    return count;
}

bool Finite(float value)
{
    return (std::bit_cast<uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
}

uint32_t ParseTraceId(const char* value)
{
    if (!value || *value < '0' || *value > '9')
        return UINT32_MAX;
    char* end = nullptr;
    const auto parsed = std::strtoul(value, &end, 10);
    return *end == '\0' && parsed <= UINT16_MAX ? static_cast<uint32_t>(parsed) : UINT32_MAX;
}

uint32_t R1ObjectId()
{
    static const uint32_t id = ParseTraceId(std::getenv("OGSR_DX12_GAME_R1_OBJECT_ID"));
    return id;
}

uint32_t TraceId()
{
    if (R1ObjectId() != UINT32_MAX)
        return R1ObjectId();
    static const uint32_t id = []
    {
        const char* value = SkinnedEnabled() ? std::getenv("OGSR_DX12_GAME_F44_TRACE_ID") : nullptr;
        if (!value || !*value)
            value = std::getenv("OGSR_DX12_GAME_F42_TRACE_ID");
        return ParseTraceId(value);
    }();
    return id;
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
        const std::string* name;
        Model* model;
        SkeletalModelSource source;
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

    const bool rigidEnabled = Enabled("OGSR_DX12_GAME_RIGID");
    const bool skinnedEnabled = SkinnedEnabled();
    shared_str tracedVisual;
    if (R1ObjectId() != UINT32_MAX)
        if (const auto* object = level.Objects.net_Find(static_cast<u16>(R1ObjectId()));
            object && object->Visual())
            tracedVisual = SourceVisualName(*object, object->Visual()->getType() == MT_SKELETON_ANIM);
    if (rigidEnabled || skinnedEnabled)
    {
        const uint32_t count = level.Objects.o_count();
        uint32_t reads = 0;
        for (uint32_t i = 0; i < count && reads < kMaxReadsPerFrame; ++i)
        {
            const auto* object = level.Objects.o_get_by_iterator(i);
            if (!object || !object->Visual() || object->getDestroy() || object->GetTmpPreDestroy() ||
                !object->getVisible())
                continue;
            const uint32_t type = object->Visual()->getType();
            const bool skinned = type == MT_SKELETON_ANIM;
            if ((skinned && (!skinnedEnabled || object->H_Parent())) ||
                (!skinned && (type != MT_SKELETON_RIGID || !rigidEnabled)))
                continue;
            const shared_str visual = SourceVisualName(*object, skinned);
            if (!*visual || m_models.contains(visual.c_str()))
                continue;
            if (m_models.size() >= kMaxAssets || batchBytes >= kMaxBatchBytes)
                break;

            ++reads;
            SkeletalModelSource source;
            std::string rejected;
            const bool loaded = ReadSkeletalModel(visual.c_str(), skinned, source, rejected);
            uint64_t sourceBytes = 0;
            for (const auto& part : source.parts)
                if (part.materialRejection.empty())
                    sourceBytes += part.mesh.vertices.size() * sizeof(Vertex) +
                        part.mesh.indices.size() * sizeof(uint16_t);
            if (sourceBytes > kMaxBatchBytes)
                rejected = "ogf_geometry_budget";
            else if (sourceBytes > kMaxBatchBytes - batchBytes)
                break;

            const auto it = m_models.try_emplace(visual.c_str()).first;
            auto& model = it->second;
            model.identity = static_cast<uint32_t>(m_models.size());
            model.boneCount = source.boneCount;
            if (!loaded || !rejected.empty())
            {
                model.reason = std::move(rejected);
                if (*tracedVisual && visual == tracedVisual)
                    Msg("ph_dx12_r1: object_asset id=%u visual='%s' state=skipped reason='%s'",
                        R1ObjectId(), visual.c_str(), model.reason.c_str());
                auto& warnings = skinned ? m_skinnedWarnings : m_rigidWarnings;
                if (warnings < 36)
                {
                    ++warnings;
                    Msg(skinned ? "ph_dx12_f44: skinned_asset skipped visual='%s' reason='%s'" :
                                  "ph_dx12_f42: rigid_asset skipped visual='%s' reason='%s'",
                        visual.c_str(), model.reason.c_str());
                }
                continue;
            }
            batchBytes += sourceBytes;
            pending.push_back({&it->first, &model, std::move(source)});
        }
    }

    std::vector<const Mesh*> sources;
    if (loadDetails && m_detailsLoaded)
        for (const auto& model : m_details.Models())
            sources.push_back(&model);
    const size_t detailCount = sources.size();
    for (const auto& entry : pending)
        for (const auto& part : entry.source.parts)
            if (part.materialRejection.empty())
                sources.push_back(&part.mesh);

    std::vector<uint32_t> handles;
    if (!sources.empty() && !scene.RegisterMeshes(owner, sources, handles, reason))
        return false;
    for (size_t index = 0; index < detailCount; ++index)
        m_frame.details.push_back({handles[index], {}});
    if (detailCount != 0)
        m_detailTextureBytes = scene.TextureFootprintBytes(std::span(handles).first(detailCount));

    const char* sceneLog = std::getenv("OGSR_DX12_GAME_SCENE_LOG");
    const bool logMaterials = sceneLog && std::strcmp(sceneLog, "1") == 0;
    size_t next = detailCount;
    for (const auto& entry : pending)
    {
        auto& model = *entry.model;
        const bool skinned = entry.source.parts.front().mesh.skinned;
        uint32_t published = 0;
        for (const auto& part : entry.source.parts)
        {
            const uint32_t handle = part.materialRejection.empty() ? handles[next++] : kInvalidMesh;
            const uint32_t partIndex = static_cast<uint32_t>(model.parts.size());
            if (*tracedVisual && *entry.name == tracedVisual.c_str())
                Msg("ph_dx12_r1: object_part id=%u visual='%s' child=%u shader='%s' texture='%s' "
                    "surface=%s mesh=%u vertices=%zu indices=%zu state=%s reason='%s'",
                    R1ObjectId(), entry.name->c_str(), partIndex, part.mesh.shader.c_str(),
                    part.mesh.texture.c_str(), SurfaceModeName(part.mesh.surface), handle,
                    part.mesh.vertices.size(), part.mesh.indices.size(),
                    handle == kInvalidMesh ? "skipped" : "registered", handle != kInvalidMesh ? "" :
                    part.materialRejection.empty() ? "mesh_or_material_unavailable" : part.materialRejection.c_str());
            const bool specialSurface = part.mesh.surface != SurfaceMode::Auto &&
                part.mesh.surface != SurfaceMode::Opaque && part.mesh.surface != SurfaceMode::Cutout;
            if (logMaterials && m_materialLogs < 96 && (specialSurface || handle == kInvalidMesh))
            {
                ++m_materialLogs;
                Msg("ph_dx12_f43: material_part visual='%s' part=%u mode=%s shader='%s' texture='%s' "
                    "indices=%zu state=%s reason='%s'", entry.name->c_str(), partIndex,
                    SurfaceModeName(part.mesh.surface), part.mesh.shader.c_str(), part.mesh.texture.c_str(),
                    part.mesh.indices.size(), handle == kInvalidMesh ? "skipped" : "registered",
                    handle != kInvalidMesh ? "" : part.materialRejection.empty() ?
                        "mesh_or_material_unavailable" : part.materialRejection.c_str());
            }
            model.parts.push_back({handle, handle == kInvalidMesh ? 0u : RequiredBones(part.mesh)});
            if (handle != kInvalidMesh)
                ++published;
            else if (skinned && m_skinnedWarnings < 36)
            {
                ++m_skinnedWarnings;
                Msg("ph_dx12_f44: skinned_part skipped visual='%s' part=%u reason='%s'",
                    entry.name->c_str(), partIndex, part.materialRejection.empty() ?
                        "mesh_or_material_unavailable" : part.materialRejection.c_str());
            }
        }
        auto& counts = skinned ? m_skinnedCounts : m_rigidCounts;
        if (published)
        {
            ++counts.newModels;
            if (skinned && m_skinnedAssetLogs < 36)
            {
                ++m_skinnedAssetLogs;
                Msg("ph_dx12_f44: skinned_asset loaded visual='%s' bones=%u parts=%u gpu_parts=%u",
                    entry.name->c_str(), model.boneCount, static_cast<uint32_t>(model.parts.size()), published);
            }
        }
        else
            model.reason = "mesh_or_material_unavailable";
    }
    return true;
}

void GameVisuals::Capture(IGame_Level& level, const float eye[3])
{
    m_objects = level.Objects.o_count();
    const bool rigidEnabled = Enabled("OGSR_DX12_GAME_RIGID");
    const bool skinnedEnabled = SkinnedEnabled();
    if (!rigidEnabled && !skinnedEnabled)
        return;
    const char* discovery = std::getenv("OGSR_DX12_GAME_F42_DISCOVERY");
    const bool discover = m_discoveryFrames < 3 && discovery && std::strcmp(discovery, "1") == 0;
    const char* skinnedDiscovery = std::getenv("OGSR_DX12_GAME_F44_DISCOVERY");
    const bool discoverSkinned = m_skinnedDiscoveryFrames < 3 && skinnedDiscovery &&
        std::strcmp(skinnedDiscovery, "1") == 0;
    uint32_t discovered = 0;
    uint32_t discoveredSkinned = 0;
    static const bool skyHemiEnabled = []
    {
        const char* hemi = std::getenv("OGSR_DX12_GAME_HEMI");
        const char* lighting = std::getenv("OGSR_DX12_GAME_LIGHTING");
        return hemi && lighting && std::strcmp(hemi, "1") == 0 && std::strcmp(lighting, "1") == 0;
    }();
    struct HemiUpdate
    {
        CObject* object = nullptr;
        Fvector position{};
        size_t snapshotIndex = 0;
        uint32_t levelIndex = 0;
    };
    HemiUpdate firstHemiUpdate, nextHemiUpdate;
    for (uint32_t i = 0; i < m_objects; ++i)
    {
        CObject* object = level.Objects.o_get_by_iterator(i);
        IRenderVisual* visual = object ? object->Visual() : nullptr;
        if (!visual)
            continue;
        ++m_visuals;
        const uint32_t type = visual->getType();
        const bool skinned = type == MT_SKELETON_ANIM;
        if ((skinned && !skinnedEnabled) ||
            (!skinned && (!rigidEnabled || type != MT_SKELETON_RIGID)))
        {
            ++m_unsupported;
            continue;
        }
        auto& counts = skinned ? m_skinnedCounts : m_rigidCounts;
        ++counts.supported;
        if (object->getDestroy() || object->GetTmpPreDestroy())
        {
            ++counts.destroyed;
            continue;
        }
        // Присоединённые визуалы рисуются владельцем со своим attachment transform.
        if (!object->getVisible() || (skinned && object->H_Parent()))
        {
            ++counts.hidden;
            continue;
        }
        const shared_str visualName = SourceVisualName(*object, skinned);
        if (!*visualName)
        {
            ++counts.missingModel;
            ++counts.rejections["missing_visual_name"];
            continue;
        }
        const auto model = m_models.find(visualName.c_str());
        if (model == m_models.end())
        {
            ++counts.waitingModel;
            continue;
        }
        if (!model->second.reason.empty())
        {
            ++counts.missingModel;
            ++counts.rejections[model->second.reason];
            continue;
        }
        ++counts.reused;

        auto* skeleton = visual->dcast_PKinematics();
        const uint32_t bones = skeleton ? skeleton->LL_BoneCount() : 0;
        if (!bones || bones > kMaxBones || (skinned && bones != model->second.boneCount) ||
            !object->Dx12Generation())
        {
            ++counts.invalidPose;
            continue;
        }
        if (bones > kMaxFrameTransforms - m_frame.poses.size())
        {
            ++counts.poseBudget;
            continue;
        }
        // Вызывается после FrameMove. Тот же путь CKinematics использует R4.
        skeleton->CalculateBones(TRUE);
        const uint64_t visibleChildren = skeleton->RigidVisibleChildMask();
        ObjectDraw entry;
        entry.id = object->ID();
        entry.generation = object->Dx12Generation();
        entry.visual = model->second.identity;
        entry.skinned = skinned;
        entry.poseOffset = static_cast<uint32_t>(m_frame.poses.size());
        entry.boneCount = bones;
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
            // LL_GetTransform_R уже содержит inverse bind. При row-vector
            // соглашении mul_43(world, skin) даёт skin * world для DX12.
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
            ++counts.invalidPose;
            continue;
        }
        ++counts.validPose;

        const auto& parts = model->second.parts;
        for (size_t part = 0; part < parts.size(); ++part)
        {
            if (!(visibleChildren & (uint64_t{1} << part)))
                continue;
            if (parts[part].mesh == kInvalidMesh || parts[part].requiredBones > bones)
            {
                ++counts.missingParts;
                continue;
            }
            m_frame.objectMeshes.push_back(parts[part].mesh);
            ++entry.meshCount;
        }
        if (entry.meshCount == 0)
        {
            m_frame.poses.resize(entry.poseOffset);
            ++counts.childHidden;
            continue;
        }
        const float distance = std::hypot(center.x - eye[0], center.y - eye[1], center.z - eye[2]);
        if (skyHemiEnabled)
        {
            auto* ros = dynamic_cast<CROS_impl*>(object->renderable.pROS);
            if (ros)
                entry.skyHemi = ros->Dx12SkyHemi();
            if (distance < 40.0f + entry.radius && object->renderable.pROS_Allowed)
            {
                const Fvector position{center.x, center.y + 0.3f * entry.radius, center.z};
                if (!ros || ros->NeedsDx12SkyHemi(position))
                {
                    const HemiUpdate update{object, position, m_frame.objects.size(), i};
                    if (!firstHemiUpdate.object)
                        firstHemiUpdate = update;
                    if (!nextHemiUpdate.object && i >= m_nextHemiObject)
                        nextHemiUpdate = update;
                }
            }
        }
        if (!skinned && distance < CROW_RADIUS2 + entry.radius)
            object->MakeMeCrow();
        auto& discoveryCount = skinned ? discoveredSkinned : discovered;
        if ((skinned ? discoverSkinned : discover) && discoveryCount < 80 && distance < 25.0f)
        {
            Msg(skinned ? "ph_dx12_f44: skinned_nearby id=%u generation=%llu section='%s' visual='%s' "
                          "bones=%u parts=%u distance=%.3f position=(%.3f,%.3f,%.3f)" :
                          "ph_dx12_f42: nearby id=%u generation=%llu section='%s' visual='%s' "
                          "bones=%u parts=%u distance=%.3f position=(%.3f,%.3f,%.3f)",
                entry.id, entry.generation, object->cNameSect_str(), visualName.c_str(),
                bones, entry.meshCount, distance, entry.world._41, entry.world._42, entry.world._43);
            ++discoveryCount;
        }
        m_frame.objects.push_back(entry);
    }
    const auto& hemiUpdate = nextHemiUpdate.object ? nextHemiUpdate : firstHemiUpdate;
    if (hemiUpdate.object)
    {
        m_nextHemiObject = hemiUpdate.levelIndex + 1;
        if (auto* ros = dynamic_cast<CROS_impl*>(hemiUpdate.object->ROS()))
        {
            ros->SampleDx12SkyHemi(hemiUpdate.position, *hemiUpdate.object);
            m_frame.objects[hemiUpdate.snapshotIndex].skyHemi = ros->Dx12SkyHemi();
        }
    }
    if (discover)
        m_discoveryFrames = m_rigidCounts.waitingModel == 0 ? 3 : m_discoveryFrames + 1;
    if (discoverSkinned)
        m_skinnedDiscoveryFrames = m_skinnedCounts.waitingModel == 0 ? 3 : m_skinnedDiscoveryFrames + 1;
}

bool GameVisuals::Prepare(IGame_Level& level, const float eye[3], GameDx12StaticScene12& scene,
                          const GameDx12StaticSceneOwner12& owner, std::string& reason)
{
    if (m_frame.session != scene.LevelSession() || m_frame.epoch != scene.LevelEpoch())
        Reset();
    m_frame.session = scene.LevelSession();
    m_frame.epoch = scene.LevelEpoch();
    m_frame.registryGeneration = scene.Generation();
    m_frame.traceId = TraceId();
    m_frame.objects.clear();
    m_frame.objectMeshes.clear();
    m_frame.poses.clear();
    m_rigidCounts = {};
    m_skinnedCounts = {};
    m_objects = 0;
    m_visuals = 0;
    m_unsupported = 0;
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

void GameVisuals::InvalidateFrame(uint64_t epoch)
{
    m_frame.epoch = epoch;
    m_frame.registryGeneration = 0;
    m_frame.objects.clear();
    m_frame.objectMeshes.clear();
    m_frame.poses.clear();
    for (auto& group : m_frame.details)
        group.transforms.clear();
}

void GameVisuals::LogPresent(uint64_t present, uint64_t fence,
    const GameDx12StaticScene12::AuxStats& draws, const GameDx12StaticScene12::AuxStats& resources) const
{
    if (R1ObjectId() != UINT32_MAX && (present <= 3 || present % 120 == 0))
    {
        const auto it = std::find_if(m_frame.objects.begin(), m_frame.objects.end(),
            [](const ObjectDraw& entry) { return entry.id == R1ObjectId(); });
        Msg("ph_dx12_r1: object_frame present=%llu id=%u state=%s parts=%u draws=%u",
            present, R1ObjectId(), it == m_frame.objects.end() ? "absent_from_snapshot" :
            draws.tracedDraws == 0 ? "snapshot_without_draw" : "submitted",
            it == m_frame.objects.end() ? 0u : it->meshCount, draws.tracedDraws);
    }
    else if (R1ObjectId() == UINT32_MAX && present <= 480 && m_frame.traceId != UINT32_MAX)
    {
        const uint32_t id = m_frame.traceId;
        const auto it = std::find_if(m_frame.objects.begin(), m_frame.objects.end(),
            [id](const ObjectDraw& entry) { return entry.id == id; });
        if (it == m_frame.objects.end())
            Msg(SkinnedEnabled() && std::getenv("OGSR_DX12_GAME_F44_TRACE_ID") ?
                    "ph_dx12_f44: skinned_trace present=%llu fence=%llu session=%llu id=%u state=absent" :
                    "ph_dx12_f42: trace present=%llu fence=%llu session=%llu id=%u state=absent",
                present, fence, m_frame.session, id);
        else
        {
            Msg(it->skinned ? "ph_dx12_f44: skinned_trace present=%llu fence=%llu session=%llu id=%u "
                              "generation=%llu state=%s visual=%u parts=%u draws=%u "
                              "world_hash=%016llx pose_hash=%016llx world=(%.4f,%.4f,%.4f)" :
                              "ph_dx12_f42: trace present=%llu fence=%llu session=%llu id=%u "
                              "generation=%llu state=%s visual=%u parts=%u draws=%u "
                              "world_hash=%016llx pose_hash=%016llx world=(%.4f,%.4f,%.4f)",
                present, fence, m_frame.session, id, it->generation,
                draws.tracedDraws != 0 ? "submitted" : "culled",
                it->visual, it->meshCount, draws.tracedDraws, Hash(std::span(&it->world, 1)),
                Hash(std::span(m_frame.poses).subspan(it->poseOffset, it->boneCount)),
                it->world._41, it->world._42, it->world._43);
        }
    }
    if (present != 1 && present != 30 && present % 120 != 0)
        return;
    Msg("ph_dx12_f42: rigid present=%llu fence=%llu objects=%u visuals=%u supported_type=%u "
        "unsupported_type=%u destroyed=%u hidden=%u valid_pose=%u invalid_pose=%u child_hidden=%u "
        "waiting_model=%u rejected_model=%u missing_parts=%u pose_budget=%u "
        "visible=%u culled=%u submitted=%u draws=%u new_models=%u reused_objects=%u",
        present, fence, m_objects, m_visuals, m_rigidCounts.supported, m_unsupported,
        m_rigidCounts.destroyed, m_rigidCounts.hidden, m_rigidCounts.validPose, m_rigidCounts.invalidPose,
        m_rigidCounts.childHidden, m_rigidCounts.waitingModel, m_rigidCounts.missingModel,
        m_rigidCounts.missingParts, m_rigidCounts.poseBudget, draws.rigidVisible, draws.rigidCulled,
        draws.rigidSubmitted, draws.rigidDraws, m_rigidCounts.newModels, m_rigidCounts.reused);
    for (const auto& [reason, count] : m_rigidCounts.rejections)
        Msg("ph_dx12_f42: rigid_skip present=%llu reason='%s' objects=%u", present, reason.c_str(), count);
    if (SkinnedEnabled())
    {
        const auto& counts = m_skinnedCounts;
        Msg("ph_dx12_f44: skinned present=%llu fence=%llu supported_type=%u destroyed=%u "
            "hidden=%u valid_pose=%u invalid_pose=%u child_hidden=%u waiting_model=%u "
            "rejected_model=%u missing_parts=%u pose_budget=%u visible=%u culled=%u "
            "submitted=%u draws=%u new_models=%u reused_objects=%u",
            present, fence, counts.supported, counts.destroyed, counts.hidden, counts.validPose,
            counts.invalidPose, counts.childHidden, counts.waitingModel, counts.missingModel,
            counts.missingParts, counts.poseBudget, draws.animatedVisible, draws.animatedCulled,
            draws.animatedSubmitted, draws.animatedDraws, counts.newModels, counts.reused);
        for (const auto& [reason, count] : counts.rejections)
            Msg("ph_dx12_f44: skinned_skip present=%llu reason='%s' objects=%u", present, reason.c_str(), count);
    }
    Msg("ph_dx12_f42: resources meshes=%u geometry_bytes=%llu new_textures=%u texture_bytes=%llu",
        resources.meshesUploaded, resources.geometryBytes, resources.texturesUploaded, resources.textureBytes);
    if (!m_detailsLoaded)
        return;
    const auto& s = m_detailStats;
    Msg("ph_dx12_f42: details present=%llu models=%u slots=%u occupied_slots=%u "
        "geometry_bytes=%llu texture_footprint_bytes=%llu source=%u eligible=%u visible=%u submitted=%u draws=%u "
        "skipped_surface=%u skipped_slope=%u skipped_near=%u skipped_distance=%u skipped_lod=%u "
        "skipped_frustum=%u skipped_material=%u skipped_budget=%u pending_slots=%u "
        "budget_slots=%u invalid_entries=%u invalid_models=%u",
        present, s.models, s.slots, s.occupiedSlots, s.geometryBytes, m_detailTextureBytes, s.source, s.eligible,
        draws.detailVisible, draws.detailSubmitted, draws.detailDraws, s.noSurface, s.slopeRejected,
        s.nearExcluded, s.distanceCulled, s.lodCulled, draws.detailCulled, draws.detailMissingMaterial,
        draws.detailBudget + s.candidateBudget, s.pendingSlots, s.cacheBudgetSlots,
        s.invalidEntries, s.invalidModels);
}
}

#endif
