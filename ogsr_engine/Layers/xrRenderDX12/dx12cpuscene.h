#pragma once

// F3.2C: подготовка CPU-сцены для standalone DX12 Smoke Test.
//
// Компонент существует потому, что в smoke-приложении НЕТ покадровой
// CPU-работы по объектам: два куба статичны, мировые координаты запечены в
// вершины (dx12mesh.h:8-10), а единственная покадровая работа — одна матрица
// камеры (CHW12.cpp:1260). Выделить существующую работу неоткуда, поэтому
// CPU-сцена создаётся здесь как отдельный opt-in этап.
//
// Подключён исключительно к xrDX12Smoke.vcxproj. Основной рендерер X-Ray
// (xrRender_R5_DX12) его не использует.
//
// Инварианты:
//  - входные данные (ObjectDesc) неизменяемы и генерируются детерминированно;
//  - каждый worker пишет ТОЛЬКО в свой диапазон [begin, end) массива результатов,
//    поэтому конкурентных записей в одну структуру нет по построению;
//  - снимок камеры формируется на главном потоке до запуска заданий, из workers
//    состояние рендерера не читается;
//  - Serial и Parallel используют один и тот же алгоритм и одни и те же входные
//    данные, поэтому результаты сравнимы поэлементно.

#include <cstdint>
#include <atomic>

#include <DirectXMath.h>

namespace gr
{
namespace jobs
{
class JobSystem;
}

namespace jobs
{
struct JobStats;
}
}

namespace dx12cpuscene
{

// Неизменяемое описание объекта. Задаётся один раз при построении таблицы.
struct ObjectDesc
{
    float orbitRadius;   // радиус орбиты вокруг начала координат
    float orbitSpeed;    // угловая скорость по орбите, рад/с
    float phase;         // начальная фаза, рад
    float height;        // базовая высота
    float bobAmplitude;  // амплитуда вертикального покачивания
    float bobSpeed;      // скорость покачивания
    float scale;         // однородный масштаб
    float spinSpeed;     // скорость вращения вокруг своей оси, рад/с
};

// Результат подготовки одного объекта за кадр.
struct ObjectState
{
    DirectX::XMFLOAT4X4 world;
    uint32_t visible;    // 1 — прошёл frustum culling, 0 — отсечён
    uint32_t pad[3];     // выравнивание до 80 байт
};

// Снимок состояния камеры. Формируется на главном потоке.
struct CameraSnapshot
{
    DirectX::XMFLOAT4X4 viewProj;
    float planes[6][4];  // left, right, bottom, top, near, far (нормали внутрь)
};

struct PrepareResult
{
    uint32_t objects = 0;
    uint32_t visible = 0;
    uint64_t checksum = 0;
    double elapsedMs = 0.0;   // полное время этапа
    double submitMs = 0.0;    // только отправка заданий (параллельный путь)
    double waitMs = 0.0;      // только ожидание (параллельный путь)
    double reduceMs = 0.0;    // подсчёт видимых объектов и checksum после подготовки
};

// Детерминированная таблица объектов. Одинаковый seed даёт одинаковую таблицу,
// что обязательно для сравнения Serial и Parallel.
void BuildObjectTable(ObjectDesc* outObjects, uint32_t count, uint32_t seed);

// Снимок камеры. Собственная орбитальная камера сцены, вычисляемая из
// timeSeconds; рендерер не затрагивается и из workers недоступен.
void BuildCameraSnapshot(CameraSnapshot& outCamera, float timeSeconds, float aspect);

// Последовательный эталон. Один поток, тот же алгоритм.
PrepareResult PrepareSerial(ObjectState* outStates, const ObjectDesc* objects,
                            uint32_t count, const CameraSnapshot& camera,
                            float timeSeconds);

// Параллельный путь через GR Job System. Возвращает раздельно время отправки и
// время ожидания. grainSize — минимальный размер диапазона.
PrepareResult PrepareParallel(gr::jobs::JobSystem& system, ObjectState* outStates,
                             const ObjectDesc* objects, uint32_t count,
                             const CameraSnapshot& camera, float timeSeconds,
                             uint32_t grainSize, std::atomic<uint32_t>* writeCounts = nullptr);

// Отдельный контроль цены пустого задания с тем же разбиением. Это оценка
// накладных расходов, не вычитаемая из времени реальной подготовки.
double MeasureDispatchOverhead(gr::jobs::JobSystem& system, uint32_t count, uint32_t grainSize);

// Автоматические CPU-проверки без окна/GPU; результат записывается в smoke log.
bool RunSelfTests();

// Построчное сравнение Serial и Parallel с допуском. Возвращает число
// расходящихся объектов и максимальное отклонение матрицы.
uint32_t CompareStates(const ObjectState* reference, const ObjectState* candidate,
                       uint32_t count, float tolerance, float* outMaxDeviation);

// Дамп результата для диагностики.
void FormatResult(char* buffer, size_t bufferSize, const char* pathName,
                  const PrepareResult& result);

} // namespace dx12cpuscene
