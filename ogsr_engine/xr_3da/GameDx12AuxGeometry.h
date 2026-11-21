#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gameaux12
{
constexpr uint32_t kMaxBones = 64;
constexpr uint32_t kInvalidMesh = UINT32_MAX;
constexpr uint32_t kMaxFrameTransforms = 131072;

struct Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t bone = 0;
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::string texture;
    bool cutout = false;
};

struct Part
{
    uint32_t mesh = kInvalidMesh;
    uint64_t boneMask = 0;
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
    DirectX::XMFLOAT4X4 world;
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
    uint32_t traceId = UINT32_MAX;
    std::vector<ObjectDraw> objects;
    std::vector<uint32_t> objectMeshes;
    std::vector<DirectX::XMFLOAT4X4> poses;
    std::vector<DetailDraw> details;
};
}
