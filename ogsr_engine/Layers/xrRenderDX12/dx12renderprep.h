#pragma once
#include <DirectXMath.h>
#include <array>
#include <cstdint>
namespace gr::jobs { class JobSystem; }
namespace renderprep12 {
enum class Mode { Serial, Parallel, Verify };
struct Object12 {
    uint32_t id{}, firstVertex{}, vertexCount{};
    DirectX::XMFLOAT4X4 world{};
    DirectX::XMFLOAT3 minimum{}, maximum{};
};
// Владеет копиями. После конструктора нет изменяющего API или внешних ссылок.
class RenderSceneSnapshot12 {
public:
    RenderSceneSnapshot12(const DirectX::XMFLOAT4X4& cpuViewProjection,
        const std::array<Object12, 2>& objects, uint64_t generation);
    const DirectX::XMFLOAT4X4& Camera() const { return m_camera; }
    const DirectX::XMFLOAT4X4& ShaderCamera() const { return m_shaderCamera; }
    const std::array<Object12, 2>& Objects() const { return m_objects; }
    uint64_t Generation() const { return m_generation; }
private:
    const DirectX::XMFLOAT4X4 m_camera, m_shaderCamera;
    const std::array<Object12, 2> m_objects;
    const uint64_t m_generation;
};
struct DrawPacket12 {
    Object12 object;
    DirectX::XMFLOAT3 worldMinimum{}, worldMaximum{};
    bool visible{}; // Диагностика: никогда не исключает Draw.
};
struct PreparedFrame12 {
    DirectX::XMFLOAT4X4 shaderCamera{};
    std::array<DrawPacket12, 2> packets{};
    uint64_t generation{};
};
struct Timings12 { double serial{}, parallel{}, submit{}, wait{}, merge{}; };
// Возврат по значению: при ошибке частичный кадр не публикуется.
PreparedFrame12 Prepare(const RenderSceneSnapshot12&, uint64_t liveGeneration,
    Mode, gr::jobs::JobSystem*, Timings12&);
bool Equivalent(const PreparedFrame12&, const PreparedFrame12&);
bool RunTests();
}
