#pragma once
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace gr::jobs { class JobSystem; }
namespace renderprep12 {
enum class Mode { Serial, Parallel, Verify };
struct Resource12 { uint32_t id{1}, generation{1}; bool operator==(const Resource12&) const = default; };
struct Sphere12 { DirectX::XMFLOAT3 center{}; float radius{}; };
enum ObjectFlags12 : uint32_t { Hud = 1, Invisible = 2, Transparent = 4 };
struct Visibility12 {
    uint32_t homFrame{}, sectorId{}, mask{~0u};
    bool homTested{};
    bool operator==(const Visibility12&) const = default;
};
struct Object12 {
    uint64_t id{};
    uint32_t firstVertex{}, vertexCount{};
    DirectX::XMFLOAT4X4 world{};
    // Локальные bounds; kernel выдаёт мировые AABB и консервативную сферу.
    DirectX::XMFLOAT3 minimum{}, maximum{};
    Sphere12 sphere{};
    Visibility12 visibility{};
    uint32_t flags{};
    float opacity{1};
    Resource12 visual{}, material{}, pso{};
};
// Только фиксированная синтетическая таблица для прототипа, не resource registry.
inline constexpr std::array<Resource12,4> SyntheticResources12{{{1,1},{2,3},{3,2},{4,7}}};
// Владеет копиями. После конструктора нет изменяющего API или внешних ссылок.
class RenderSceneSnapshot12 {
public:
    RenderSceneSnapshot12(const DirectX::XMFLOAT4X4& cpuViewProjection,
        std::span<const Object12> objects, uint64_t generation);
    const DirectX::XMFLOAT4X4& Camera() const { return m_camera; }
    const DirectX::XMFLOAT4X4& ShaderCamera() const { return m_shaderCamera; }
    const std::vector<Object12>& Objects() const { return m_objects; }
    uint64_t Generation() const { return m_generation; }
private:
    const DirectX::XMFLOAT4X4 m_camera, m_shaderCamera;
    const std::vector<Object12> m_objects;
    const uint64_t m_generation;
};
struct DrawPacket12 {
    Object12 object;
    DirectX::XMFLOAT3 worldMinimum{}, worldMaximum{};
    Sphere12 worldSphere{};
    bool visible{}; // Диагностика: никогда не исключает Draw.
};
struct PreparedFrame12 {
    DirectX::XMFLOAT4X4 shaderCamera{};
    std::vector<DrawPacket12> packets;
    uint64_t generation{};
};
struct Timings12 {
    double serial{}, parallel{}, submit{}, wait{}, merge{};
    double serialPrepare{}, parallelPrepare{}, serialMerge{}, parallelMerge{};
};
// Возврат по значению: при ошибке частичный кадр не публикуется.
PreparedFrame12 Prepare(const RenderSceneSnapshot12&, uint64_t liveGeneration,
    Mode, gr::jobs::JobSystem*, Timings12&);
bool Equivalent(const PreparedFrame12&, const PreparedFrame12&);
bool RunTests();
bool RunRepresentativeTests();
}
