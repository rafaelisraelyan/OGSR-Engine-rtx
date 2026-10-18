#pragma once

#include <DirectXMath.h>
#include <windows.h>

// F3.1: орбитальная камера с perspective projection.
//
// Без COM-объектов и без зависимостей от xrCore. Единственный компонент,
// который адаптер IX-Ray возьмёт как есть: `Fmatrix` здесь был бы прямой
// связью с xrCore, которую пришлось бы выдирать.
//
// Матрицы row-major, row-vectors, pre-multiplication (DirectXMath).
// ViewProjection = view * proj. World-матрицы НЕТ: мировые координаты
// запечены в вершины (вариант A, §2.3 ТЗ), поэтому в CB уходит
// ViewProjection, а не полный WVP. Поддержка отдельной world-матрицы
// потребовала бы per-object записи в CB — ровно того, что §2.3 запрещает.
//
// Транспонирование при записи обязательно: DirectXMath хранит row-major,
// а компилятор HLSL по умолчанию упаковывает матрицы в constant buffer
// column-major. Передача «как есть» интерпретирует транспонированную
// матрицу. Запись: XMStoreFloat4x4(&dst, XMMatrixTranspose(viewProj)).

class SmokeCamera12
{
public:
    SmokeCamera12() = default;
    ~SmokeCamera12() = default;

    SmokeCamera12(const SmokeCamera12&) = delete;
    SmokeCamera12& operator=(const SmokeCamera12&) = delete;

    // Устанавливает соотношение сторон. Отказ при aspect <= 0, NaN, Inf.
    //
    // Форма проверки !(aspect > 0.0f) намеренная: одним выражением отсекает
    // и aspect <= 0, и NaN (NaN не удовлетворяет >). isfinite оставлен для
    // ясности намерения, хотя избыточен. aspect НЕ обрезается до минимума,
    // а отклоняется: XMMatrixPerspectiveFovLH при aspect <= 0 даёт деление
    // на ноль или отрицательный масштаб, то есть вывернутую картинку,
    // а не ошибку.
    bool SetAspect(float aspect);

    // Устанавливает статический ракурс камеры. Валидация ДО вызова
    // DirectXMath, а не после: поведение XMMatrixLookAtLH при eye == target
    // в документации не специфицировано как гарантированный NaN, библиотека
    // содержит assert, но assert отключён в Release (проект собирается с
    // NDEBUG), и в релизной сборке результат может оказаться неопределённым.
    //
    // Отказ = return false, предыдущая валидная матрица СОХРАНЯЕТСЯ.
    bool SetLookAt(
        DirectX::XMVECTOR eye,
        DirectX::XMVECTOR target,
        DirectX::XMVECTOR up);

    // Обновляет орбитальную камеру по времени (секунды). Yaw растёт линейно,
    // pitch фиксирован. Мировые координаты кубов не меняются — вращается
    // камера. Пользовательский ввод не добавляется.
    void SetOrbit(float timeSeconds);

    // Возвращает фиксированный ракурс для детерминированных тестов и
    // сравнения кадров между прогонами.
    void GetFixedPose(
        DirectX::XMVECTOR& eye,
        DirectX::XMVECTOR& target,
        DirectX::XMVECTOR& up) const;

    // Вычисляет ViewProjection = view * proj и транспонирует в out.
    // Возвращает false, если аргументы невалидны (вызывающий код уже
    // проверил их при установке, но проверка на месте защищает от
    // прямого вызова с невалидным состоянием).
    bool BuildViewProjection(DirectX::XMFLOAT4X4& out);

    float GetFovRadians() const { return m_fov; }
    float GetNear() const { return m_nearZ; }
    float GetFar() const { return m_farZ; }
    float GetAspect() const { return m_aspect; }

private:
    static constexpr float kFovDegrees = 60.0f;
    static constexpr float kNearZ = 0.1f;
    static constexpr float kFarZ = 100.0f;

    // Скорость орбиты: радиан в секунду. Выбрана так, чтобы полный оборот
    // занимал ~25 секунд — достаточно медленно для визуального наблюдения
    // и достаточно быстро, чтобы движение было заметно в коротком прогоне.
    static constexpr float kOrbitSpeed = 0.25f;
    static constexpr float kOrbitPitch = 0.35f;
    static constexpr float kOrbitRadius = 6.0f;

    float m_fov = DirectX::XMConvertToRadians(kFovDegrees);
    float m_nearZ = kNearZ;
    float m_farZ = kFarZ;
    float m_aspect = 1.0f;

    // Фиксированный ракурс по умолчанию. Совпадает с GetFixedPose().
    DirectX::XMVECTOR m_eye = DirectX::XMVectorSet(0.0f, 0.0f, -6.0f, 1.0f);
    DirectX::XMVECTOR m_target = DirectX::XMVectorZero();
    DirectX::XMVECTOR m_up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
};

// Детерминированные CPU-проверки математики без GPU. Объявлена здесь,
// чтобы отдельное тестовое приложение могло вызвать без линковки всего
// CHW12. Реализация — в dx12camera.cpp.
bool TestCameraMath();
