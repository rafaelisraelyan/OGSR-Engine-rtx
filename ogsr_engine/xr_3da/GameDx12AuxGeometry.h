#pragma once

#include "GameDx12Surface.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gameaux12
{
constexpr uint32_t kMaxBones = 128;
constexpr uint32_t kInvalidMesh = UINT32_MAX;
constexpr uint32_t kMaxFrameTransforms = 131072;

struct Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t bones[4]{};
    float weights[4]{1.0f, 0.0f, 0.0f, 0.0f};
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::string texture;
    bool cutout = false;
    bool skinned = false;
    std::string shader;
    SurfaceMode surface = SurfaceMode::Auto;
};

struct Part
{
    uint32_t mesh = kInvalidMesh;
    uint32_t requiredBones = 0;
};

struct ObjectDraw
{
    uint16_t id = 0;
    uint64_t generation = 0;
    uint32_t visual = 0;
    uint32_t firstMesh = 0;
    uint32_t meshCount = 0;
    uint32_t poseOffset = 0;
    uint32_t boneCount = 0;
    float center[3]{};
    float radius = 0.0f;
    float skyHemi = 0.0f;
    DirectX::XMFLOAT4X4 world;
    bool skinned = false;
};

struct DetailDraw
{
    uint32_t mesh = kInvalidMesh;
    std::vector<DirectX::XMFLOAT4X4> transforms;
};

struct Frame
{
    uint64_t session = 0;
    uint64_t epoch = 0;
    uint64_t registryGeneration = 0;
    uint32_t traceId = UINT32_MAX;
    std::vector<ObjectDraw> objects;
    std::vector<uint32_t> objectMeshes;
    std::vector<DirectX::XMFLOAT4X4> poses;
    std::vector<DetailDraw> details;
};
}
