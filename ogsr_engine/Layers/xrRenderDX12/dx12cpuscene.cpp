#include "dx12cpuscene.h"

#include "gr/jobs/JobSystem.h"
#include "gr/jobs/JobGroup.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace dx12cpuscene
{

namespace
{

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Детерминированный ГПСЧ: одинаковый seed обязан давать одинаковую таблицу,
// иначе сравнение Serial и Parallel теряет смысл.
struct DeterministicRng
{
    uint64_t state;

    explicit DeterministicRng(uint32_t seed)
        : state((static_cast<uint64_t>(seed) << 32) ^ 0x9E3779B97F4A7C15ULL)
    {
    }

    float NextFloat01()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        // Старшие 24 бита дают равномерное значение в [0, 1).
        return static_cast<float>((state >> 40) & 0xFFFFFF) / 16777216.0f;
    }
};

// Полуразмер ограничивающего куба объекта. Используется для сферического
// frustum-теста: консервативная, но дешёвая оценка.
constexpr float kObjectRadius = 0.87f;

// Единственный алгоритм вычисления одного объекта. Вызывается и из последовательного,
// и из параллельного пути — это то, что делает результаты сравнимыми.
void ComputeObject(ObjectState& outState, const ObjectDesc& object,
                   const CameraSnapshot& camera, float timeSeconds)
{
    using namespace DirectX;

    // Орбита вокруг начала координат.
    const float angle = timeSeconds * object.orbitSpeed + object.phase;
    const float bob = object.bobAmplitude * sinf(timeSeconds * object.bobSpeed + object.phase);
    const XMVECTOR position = XMVectorSet(cosf(angle) * object.orbitRadius,
                                          object.height + bob,
                                          sinf(angle) * object.orbitRadius,
                                          0.0f);

    const float spin = timeSeconds * object.spinSpeed + object.phase;

    const XMMATRIX translation = XMMatrixTranslationFromVector(position);
    const XMMATRIX rotation = XMMatrixRotationY(spin);
    const XMMATRIX scaling = XMMatrixScaling(object.scale, object.scale, object.scale);

    // M = S * R * T — масштаб, затем вращение, затем перенос.
    XMMATRIX world = XMMatrixMultiply(XMMatrixMultiply(scaling, rotation), translation);
    XMStoreFloat4x4(&outState.world, world);

    // Frustum culling по ограничивающей сфере.
    XMFLOAT3 center;
    XMStoreFloat3(&center, position);

    bool visible = true;
    for (int p = 0; p < 6 && visible; ++p)
    {
        const float* plane = camera.planes[p];
        const float distance = plane[0] * center.x + plane[1] * center.y +
                               plane[2] * center.z + plane[3];
        if (distance < -kObjectRadius * object.scale)
        {
            visible = false;
        }
    }

    outState.visible = visible ? 1u : 0u;
}

uint64_t HashState(const ObjectState& state)
{
    uint64_t hash = 1469598103934665603ULL;
    uint64_t words[8];
    std::memcpy(words, state.world.m, sizeof(words));
    for (int i = 0; i < 8; ++i)
    {
        hash ^= words[i];
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<uint64_t>(state.visible) * 0x9E3779B97F4A7C15ULL;
    return hash;
}

// Извлечение шести плоскостей пирамиды видимости из матрицы ViewProjection
// (метод Gribb-Hartmann). Нормализация обязательна для корректного теста сферы.
void ExtractFrustumPlanes(float outPlanes[6][4], const DirectX::XMFLOAT4X4& m)
{
    // XMFLOAT4X4: _RC — строка R, столбец C.
    // DirectXMath: row-vector * matrix, поэтому clip-компоненты — столбцы.
    // D3D: -w <= x,y <= w и 0 <= z <= w (near = столбец z).
    const float row1[4] = {m._11, m._21, m._31, m._41};
    const float row2[4] = {m._12, m._22, m._32, m._42};
    const float row3[4] = {m._13, m._23, m._33, m._43};
    const float row4[4] = {m._14, m._24, m._34, m._44};

    float* planes[6] = {outPlanes[0], outPlanes[1], outPlanes[2],
                        outPlanes[3], outPlanes[4], outPlanes[5]};

    for (int i = 0; i < 4; ++i)
    {
        planes[0][i] = row4[i] + row1[i];   // left
        planes[1][i] = row4[i] - row1[i];   // right
        planes[2][i] = row4[i] + row2[i];   // bottom
        planes[3][i] = row4[i] - row2[i];   // top
        planes[4][i] = row3[i];            // near
        planes[5][i] = row4[i] - row3[i];   // far
    }

    for (int p = 0; p < 6; ++p)
    {
        const float length = sqrtf(planes[p][0] * planes[p][0] +
                                   planes[p][1] * planes[p][1] +
                                   planes[p][2] * planes[p][2]);
        if (length > 1e-9f)
        {
            planes[p][0] /= length;
            planes[p][1] /= length;
            planes[p][2] /= length;
            planes[p][3] /= length;
        }
    }
}

} // namespace

void BuildObjectTable(ObjectDesc* outObjects, uint32_t count, uint32_t seed)
{
    if (outObjects == nullptr || count == 0)
    {
        return;
    }

    DeterministicRng rng(seed);
    for (uint32_t i = 0; i < count; ++i)
    {
        ObjectDesc& object = outObjects[i];
        object.orbitRadius = 1.5f + rng.NextFloat01() * 8.5f;      // 1.5 .. 10.0
        object.orbitSpeed = (rng.NextFloat01() * 2.0f - 1.0f) * 1.5f;
        object.phase = rng.NextFloat01() * 6.2831853f;
        object.height = rng.NextFloat01() * 4.0f - 2.0f;
        object.bobAmplitude = rng.NextFloat01() * 0.6f;
        object.bobSpeed = rng.NextFloat01() * 3.0f + 0.5f;
        object.scale = 0.5f + rng.NextFloat01() * 1.5f;             // 0.5 .. 2.0
        object.spinSpeed = (rng.NextFloat01() * 2.0f - 1.0f) * 2.5f;
    }
}

void BuildCameraSnapshot(CameraSnapshot& outCamera, float timeSeconds, float aspect)
{
    using namespace DirectX;

    // Собственная орбитальная камера CPU-сцены. Рендерер не затрагивается:
    // снимок строится из времени на главном потоке и передаётся заданиям
    // по значению, поэтому из workers состояние рендерера недоступно.
    const float yaw = timeSeconds * 0.25f;
    const float radius = 12.0f;
    const float height = 3.5f;

    const XMVECTOR eye = XMVectorSet(cosf(yaw) * radius, height, sinf(yaw) * radius, 0.0f);
    const XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(60.0f), aspect, 0.1f, 100.0f);
    const XMMATRIX viewProj = XMMatrixMultiply(view, projection);

    XMStoreFloat4x4(&outCamera.viewProj, viewProj);
    ExtractFrustumPlanes(outCamera.planes, outCamera.viewProj);
}

PrepareResult PrepareSerial(ObjectState* outStates, const ObjectDesc* objects,
                            uint32_t count, const CameraSnapshot& camera,
                            float timeSeconds)
{
    PrepareResult result;
    result.objects = count;

    const Clock::time_point start = Clock::now();

    uint32_t visible = 0;
    uint64_t checksum = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        ComputeObject(outStates[i], objects[i], camera, timeSeconds);
    }
    const Clock::time_point reduceStart = Clock::now();
    for (uint32_t i = 0; i < count; ++i)
    {
        visible += outStates[i].visible;
        checksum ^= HashState(outStates[i]);
    }
    const Clock::time_point end = Clock::now();

    result.visible = visible;
    result.checksum = checksum;
    result.elapsedMs = ElapsedMs(start, end);
    result.reduceMs = ElapsedMs(reduceStart, end);
    return result;
}

PrepareResult PrepareParallel(gr::jobs::JobSystem& system, ObjectState* outStates,
                             const ObjectDesc* objects, uint32_t count,
                             const CameraSnapshot& camera, float timeSeconds,
                             uint32_t grainSize, std::atomic<uint32_t>* writeCounts)
{
    if (!system.IsInitialized())
        throw std::logic_error("CPU preparation requires an initialized JobSystem");
    PrepareResult result;
    result.objects = count;

    if (grainSize == 0)
    {
        grainSize = 1;
    }

    const Clock::time_point start = Clock::now();

    // JobGroup::ParallelFor возвращается сразу после отправки, а Wait() ждёт
    // завершения. Это даёт разделить время отправки и время ожидания.
    Clock::time_point submitStart, submitEnd, waitEnd;
    {
    gr::jobs::JobGroup group = system.CreateGroup();
    submitStart = Clock::now();
    group.ParallelFor(count, grainSize,
                      [outStates, objects, &camera, timeSeconds, writeCounts](uint32_t begin, uint32_t end,
                                                                 uint32_t /*threadIndex*/)
                      {
                          // Каждый worker пишет только в свой диапазон.
                          for (uint32_t i = begin; i < end; ++i)
                          {
                              if (writeCounts && writeCounts[i].fetch_add(1) != 0)
                                  continue; // Диагностический режим фиксирует перекрытие, не создавая гонку.
                              ComputeObject(outStates[i], objects[i], camera, timeSeconds);
                          }
                      });
    submitEnd = Clock::now();

    group.Wait();
    waitEnd = Clock::now();
    }
    const auto reduceStart = Clock::now();

    uint32_t visible = 0;
    uint64_t checksum = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        visible += outStates[i].visible;
        checksum ^= HashState(outStates[i]);
    }

    result.visible = visible;
    result.checksum = checksum;
    result.submitMs = ElapsedMs(submitStart, submitEnd);
    result.waitMs = ElapsedMs(submitEnd, waitEnd);
    const Clock::time_point end = Clock::now();
    result.reduceMs = ElapsedMs(reduceStart, end);
    result.elapsedMs = ElapsedMs(start, end);
    return result;
}

double MeasureDispatchOverhead(gr::jobs::JobSystem& system, uint32_t count, uint32_t grainSize)
{
    const auto start = Clock::now();
    {
    auto group = system.CreateGroup();
    group.ParallelFor(count, grainSize, [](uint32_t, uint32_t, uint32_t) {});
    group.Wait();
    }
    return ElapsedMs(start, Clock::now());
}

uint32_t CompareStates(const ObjectState* reference, const ObjectState* candidate,
                       uint32_t count, float tolerance, float* outMaxDeviation)
{
    if (reference == nullptr || candidate == nullptr || count == 0)
    {
        if (outMaxDeviation)
        {
            *outMaxDeviation = 0.0f;
        }
        return 0;
    }

    uint32_t mismatches = 0;
    float maxDeviation = 0.0f;

    for (uint32_t i = 0; i < count; ++i)
    {
        bool different = reference[i].visible != candidate[i].visible;

        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                const float difference = reference[i].world.m[r][c] - candidate[i].world.m[r][c];
                if (!std::isfinite(reference[i].world.m[r][c]) || !std::isfinite(candidate[i].world.m[r][c]))
                {
                    different = true;
                    maxDeviation = std::numeric_limits<float>::infinity();
                    continue;
                }
                const float magnitude = difference < 0.0f ? -difference : difference;
                if (magnitude > maxDeviation)
                {
                    maxDeviation = magnitude;
                }
                if (magnitude > tolerance)
                {
                    different = true;
                }
            }
        }
        mismatches += different ? 1u : 0u;
    }

    if (outMaxDeviation)
    {
        *outMaxDeviation = maxDeviation;
    }
    return mismatches;
}

void FormatResult(char* buffer, size_t bufferSize, const char* pathName,
                  const PrepareResult& result)
{
    if (buffer == nullptr || bufferSize == 0)
    {
        return;
    }
    snprintf(buffer, bufferSize,
             "%s: objects=%u visible=%u submit=%.4f ms wait=%.4f ms total=%.4f ms checksum=%llu",
             pathName, result.objects, result.visible, result.submitMs, result.waitMs,
             result.elapsedMs, static_cast<unsigned long long>(result.checksum));
}

} // namespace dx12cpuscene
