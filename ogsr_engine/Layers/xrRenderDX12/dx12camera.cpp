#include "dx12camera.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    // Комбинированный допуск: absEps ловит расхождение около нуля, где
    // относительная погрешность бессмысленна; relEps — для элементов, чья
    // величина выходит за единицы. Один ulp при величине ~1.7 равен 2.4e-7,
    // поэтому 1e-5 — это ~40 ulp, с запасом на накопление при нескольких
    // умножениях. Чисто абсолютное сравнение неверно для матрицы, где
    // элементы имеют разный масштаб; чисто относительное неверно около
    // нуля, где часть элементов равны нулю ровно.
    constexpr float kAbsEps = 1e-5f;
    constexpr float kRelEps = 1e-4f;

    bool FloatNear(float a, float b)
    {
        const float diff = std::fabs(a - b);
        const float scale = std::fmax(std::fabs(a), std::fabs(b));
        return diff <= kAbsEps || diff <= kRelEps * scale;
    }

    // Проверка конечности вектора. NaN не сравняется само с собой, поэтому
    // условие `!(t != t)` ловит NaN. Inf ловится сравнением с
    // numeric_limits<float>::infinity(). XMVector3Dot(v, v) даёт квадрат
    // длины: для конечного вектора он конечен и не равен Inf.
    bool VectorIsFinite(DirectX::XMVECTOR v)
    {
        const DirectX::XMVECTOR t = DirectX::XMVector3Dot(v, v);
        return !(DirectX::XMVectorGetX(t) != DirectX::XMVectorGetX(t)) &&
               DirectX::XMVectorGetX(t) != std::numeric_limits<float>::infinity();
    }

    // Сравнение XMFLOAT4X4 поэлементно с допусками. Используется для
    // проверки матриц в CPU-тестах без GPU.
    bool MatricesNear(const DirectX::XMFLOAT4X4& a, const DirectX::XMFLOAT4X4& b)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!FloatNear(a.m[i][j], b.m[i][j]))
                    return false;
        return true;
    }
}

bool SmokeCamera12::SetAspect(float aspect)
{
    // Форма проверки !(aspect > 0.0f) намеренная: одним выражением отсекает
    // и aspect <= 0, и NaN (NaN не удовлетворяет >). isfinite избыточен, но
    // оставлен для ясности намерения. aspect НЕ обрезается до минимума, а
    // отклоняется: XMMatrixPerspectiveFovLH при aspect <= 0 даёт деление
    // на ноль или отрицательный масштаб, то есть вывернутую картинку,
    // а не ошибку.
    if (!(aspect > 0.0f) || !std::isfinite(aspect))
        return false;

    m_aspect = aspect;
    return true;
}

bool SmokeCamera12::SetLookAt(
    DirectX::XMVECTOR eye,
    DirectX::XMVECTOR target,
    DirectX::XMVECTOR up)
{
    // Валидация ДО вызова DirectXMath, а не после: поведение
    // XMMatrixLookAtLH при eye == target в документации не специфицировано
    // как гарантированный NaN, библиотека содержит assert, но assert
    // отключён в Release (проект собирается с NDEBUG), и в релизной сборке
    // результат может оказаться неопределённым, а не аккуратно NaN. UD на
    // входе не даёт права на предсказуемый выход.

    // 1. NaN или Inf в любой координате eye/target/up.
    if (!VectorIsFinite(eye) || !VectorIsFinite(target) || !VectorIsFinite(up))
        return false;

    // 2. eye == target: квадрат длины направления <= 1e-12.
    const DirectX::XMVECTOR dir = DirectX::XMVectorSubtract(target, eye);
    if (DirectX::XMVector3LessOrEqual(
            DirectX::XMVector3LengthSq(dir),
            DirectX::XMVectorReplicate(1e-12f)))
        return false;

    // 3. Нулевой up: квадрат длины <= 1e-12.
    if (DirectX::XMVector3LessOrEqual(
            DirectX::XMVector3LengthSq(up),
            DirectX::XMVectorReplicate(1e-12f)))
        return false;

    // 4. forward и up параллельны или практически параллельны.
    //
    // Порог 1e-6 по |cos|, НЕ 1e-12 как у остальных проверок. Причина:
    // LookAt нормализует forward и строит базис через кросс-произведение.
    // При |cos| → 1 результат нормализации up после ортогонализации
    // (Gram–Schmidt) теряет точность катастрофически: относительная ошибка
    // растёт как 1/sin(angle), и при угле в сотые доли градуса базис
    // содержит мусор. Это уже не «почти параллельно», это численно
    // некорректный базис. Порог 1e-6f соответствует примерно 0.06° —
    // заведомо ниже любого осмысленного наклона камеры, но выше области
    // потери точности.
    const DirectX::XMVECTOR dirN = DirectX::XMVector3Normalize(dir);
    const DirectX::XMVECTOR upN = DirectX::XMVector3Normalize(up);
    const float cosAngle = std::fabs(DirectX::XMVectorGetX(
        DirectX::XMVector3Dot(dirN, upN)));
    if (cosAngle >= 1.0f - 1e-6f)
        return false;

    // Все проверки пройдены: устанавливаем ракурс.
    m_eye = eye;
    m_target = target;
    m_up = up;
    return true;
}

void SmokeCamera12::SetOrbit(float timeSeconds)
{
    const float yaw = timeSeconds * kOrbitSpeed;
    const float cosPitch = std::cos(kOrbitPitch);
    const float sinPitch = std::sin(kOrbitPitch);
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);

    // Камера кружится вокруг origin по окружности радиуса kOrbitRadius.
    // Target и up фиксированы: движется только камера, мировые координаты
    // кубов не меняются.
    m_eye = DirectX::XMVectorSet(
        cosYaw * cosPitch * kOrbitRadius,
        sinPitch * kOrbitRadius,
        sinYaw * cosPitch * kOrbitRadius,
        1.0f);
    m_target = DirectX::XMVectorZero();
    m_up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
}

void SmokeCamera12::GetFixedPose(
    DirectX::XMVECTOR& eye,
    DirectX::XMVECTOR& target,
    DirectX::XMVECTOR& up) const
{
    // Детерминированный ракурс для тестов и сравнения кадров между
    // прогонами. Камера смотрит на origin сверху вниз под небольшим углом.
    eye = DirectX::XMVectorSet(0.0f, 0.0f, -6.0f, 1.0f);
    target = DirectX::XMVectorZero();
    up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
}

DirectX::XMFLOAT4X4 SmokeCamera12::CaptureViewProjection() const
{
    DirectX::XMFLOAT4X4 out;
    const auto view = DirectX::XMMatrixLookAtLH(m_eye, m_target, m_up);
    const auto proj = DirectX::XMMatrixPerspectiveFovLH(m_fov, m_aspect, m_nearZ, m_farZ);
    DirectX::XMStoreFloat4x4(&out, DirectX::XMMatrixMultiply(view, proj));
    return out;
}

bool SmokeCamera12::BuildViewProjection(DirectX::XMFLOAT4X4& out)
{
    const auto captured = CaptureViewProjection();
    const auto viewProj = DirectX::XMLoadFloat4x4(&captured);

    // Транспонирование обязательно: DirectXMath хранит row-major, а
    // компилятор HLSL по умолчанию упаковывает матрицы в constant buffer
    // column-major (каждый столбец занимает отдельный constant register).
    // Передача «как есть» интерпретирует транспонированную матрицу.
    DirectX::XMStoreFloat4x4(&out, DirectX::XMMatrixTranspose(viewProj));
    return true;
}

// ---------------------------------------------------------------------------
// Детерминированные CPU-проверки без GPU.
//
// Это единственная защита от неверного transpose/порядка матриц, который
// невидим для validation: неверный порядок даёт зеркальное или перевёрнутое
// изображение при нулевых validation errors.
// ---------------------------------------------------------------------------

// Эталон чистой проекции (§6.5 ТЗ), row-major, ДО транспонирования:
//   _11 = 0.9742786  _22 = 1.7320508  _33 = 1.0010010  _34 = 1.0
//   _43 = -0.1001001 _44 = 0.0          остальные 0.0
//
// ВАЖНО: эталон относится к ЧИСТОЙ проекции. НЕ сравнивай с ним
// ViewProjection — такой тест провалится на корректной реализации,
// потому что view домножается слева и перемешивает элементы.
static bool TestPureProjection()
{
    const DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    DirectX::XMFLOAT4X4 m;
    DirectX::XMStoreFloat4x4(&m, proj);

    DirectX::XMFLOAT4X4 expected = {};
    expected.m[0][0] = 0.9742786f;  // _11
    expected.m[1][1] = 1.7320508f;  // _22
    expected.m[2][2] = 1.0010010f;  // _33
    expected.m[2][3] = 1.0f;        // _34
    expected.m[3][2] = -0.1001001f; // _43
    expected.m[3][3] = 0.0f;        // _44
    // остальные 0.0

    if (!MatricesNear(m, expected))
    {
        std::printf("  FAIL: pure proj mismatch (got _11=%.7f _22=%.7f _33=%.7f _34=%.7f _43=%.7f _44=%.7f)\n",
                    m.m[0][0], m.m[1][1], m.m[2][2], m.m[2][3], m.m[3][2], m.m[3][3]);
        return false;
    }
    std::printf("  ok:   pure proj matches §6.5\n");
    return true;
}

// Эталон композиции view * proj для фиксированного ракурса
// eye=(0,0,-6), target=(0,0,0), up=(0,1,0), row-major, ДО транспонирования:
//
//   view = [ 1 0 0 0; 0 1 0 0; 0 0 1 0; 0 0 6 1 ]
//   viewProj = view * proj:
//     _11 = proj._11            (view.r[0] = (1,0,0,0) копирует proj.r[0])
//     _22 = proj._22            (view.r[1] = (0,1,0,0) копирует proj.r[1])
//     _33 = proj._33            (view.r[2] = (0,0,1,0) копирует proj.r[2])
//     _34 = proj._34 = 1.0      (то же)
//     _43 = 6*proj._33 + proj._43 = 6*1.0010010 + (-0.1001001) = 5.9059059
//     _44 = 6*proj._34 + proj._44 = 6*1.0 + 0.0 = 6.0
//
// Вывод: view.r[3] = (0,0,6,1) добавляет 6-кратную строку proj.r[2] к
// proj.r[3], поэтому только _43 и _44 отличаются от чистой proj.
static bool TestComposedViewProjection()
{
    const DirectX::XMVECTOR eye = DirectX::XMVectorSet(0.0f, 0.0f, -6.0f, 1.0f);
    const DirectX::XMVECTOR target = DirectX::XMVectorZero();
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);

    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, target, up);
    const DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);

    DirectX::XMFLOAT4X4 m;
    DirectX::XMStoreFloat4x4(&m, viewProj);

    DirectX::XMFLOAT4X4 expected = {};
    expected.m[0][0] = 0.9742786f;  // _11
    expected.m[1][1] = 1.7320508f;  // _22
    expected.m[2][2] = 1.0010010f;  // _33
    expected.m[2][3] = 1.0f;        // _34
    expected.m[3][2] = 5.9059059f;  // _43 = 590/99.9
    expected.m[3][3] = 6.0f;        // _44

    if (!MatricesNear(m, expected))
    {
        std::printf("  FAIL: view*proj mismatch (got _11=%.7f _22=%.7f _33=%.7f _34=%.7f _43=%.7f _44=%.7f)\n",
                    m.m[0][0], m.m[1][1], m.m[2][2], m.m[2][3], m.m[3][2], m.m[3][3]);
        return false;
    }
    std::printf("  ok:   view*proj matches independent calculation\n");
    return true;
}

// Каждый отказ SetLookAt возвращает false И НЕ меняет ранее установленную
// матрицу (сравнение с снимком до вызова).
static bool TestSetLookAtRejections()
{
    SmokeCamera12 cam;

    // Устанавливаем валидный ракурс и снимок матрицы.
    const DirectX::XMVECTOR eye = DirectX::XMVectorSet(0.0f, 0.0f, -6.0f, 1.0f);
    const DirectX::XMVECTOR target = DirectX::XMVectorZero();
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    if (!cam.SetLookAt(eye, target, up))
    {
        std::printf("  FAIL: SetLookAt(valid) returned false\n");
        return false;
    }

    DirectX::XMFLOAT4X4 before;
    cam.BuildViewProjection(before);

    // 1. NaN в eye.
    if (cam.SetLookAt(
            DirectX::XMVectorSet(std::numeric_limits<float>::quiet_NaN(), 0.0f, -6.0f, 1.0f),
            target, up))
    {
        std::printf("  FAIL: SetLookAt(NaN eye) returned true\n");
        return false;
    }

    // 2. Inf в target.
    if (cam.SetLookAt(
            eye,
            DirectX::XMVectorSet(0.0f, std::numeric_limits<float>::infinity(), 0.0f, 1.0f),
            up))
    {
        std::printf("  FAIL: SetLookAt(Inf target) returned true\n");
        return false;
    }

    // 3. eye == target.
    if (cam.SetLookAt(
            DirectX::XMVectorZero(),
            DirectX::XMVectorZero(),
            up))
    {
        std::printf("  FAIL: SetLookAt(eye==target) returned true\n");
        return false;
    }

    // 4. up == 0.
    if (cam.SetLookAt(eye, target, DirectX::XMVectorZero()))
    {
        std::printf("  FAIL: SetLookAt(up==0) returned true\n");
        return false;
    }

    // 5. up == forward (параллельны): eye=(0,0,-6), target=(0,0,0), up=(0,0,1).
    if (cam.SetLookAt(
            eye, target,
            DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f)))
    {
        std::printf("  FAIL: SetLookAt(up==forward) returned true\n");
        return false;
    }

    // 6. up == -forward (антипараллельны): up=(0,0,-1).
    if (cam.SetLookAt(
            eye, target,
            DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 1.0f)))
    {
        std::printf("  FAIL: SetLookAt(up==-forward) returned true\n");
        return false;
    }

    // Матрица не изменилась ни после одного из отказов.
    DirectX::XMFLOAT4X4 after;
    cam.BuildViewProjection(after);
    if (!MatricesNear(before, after))
    {
        std::printf("  FAIL: SetLookAt rejected calls changed matrix\n");
        return false;
    }

    std::printf("  ok:   SetLookAt rejects preserve matrix\n");
    return true;
}

// Каждый отказ SetAspect: aspect = 0, -1, NaN.
static bool TestSetAspectRejections()
{
    SmokeCamera12 cam;

    if (!cam.SetAspect(16.0f / 9.0f))
    {
        std::printf("  FAIL: SetAspect(16/9) returned false\n");
        return false;
    }

    bool ok = true;

    if (cam.SetAspect(0.0f))
    {
        std::printf("  FAIL: SetAspect(0) returned true\n");
        ok = false;
    }

    if (cam.SetAspect(-1.0f))
    {
        std::printf("  FAIL: SetAspect(-1) returned true\n");
        ok = false;
    }

    if (cam.SetAspect(std::numeric_limits<float>::quiet_NaN()))
    {
        std::printf("  FAIL: SetAspect(NaN) returned true\n");
        ok = false;
    }

    if (ok)
        std::printf("  ok:   SetAspect rejects 0, -1, NaN\n");
    return ok;
}

// Идентичность: ViewProjection = Identity * proj == proj поэлементно.
//
// Устанавливаем eye=(0,0,0), target=(0,0,1), up=(0,1,0). Тогда
// zaxis = normalize(target - eye) = (0,0,1), xaxis = normalize(cross(up,
// zaxis)) = (1,0,0), yaxis = cross(zaxis, xaxis) = (0,1,0), и view =
// Identity (все смещения нулевые, потому что eye = origin). Следовательно
// view * proj = Identity * proj = proj.
static bool TestIdentityView()
{
    SmokeCamera12 cam;

    const DirectX::XMVECTOR eye = DirectX::XMVectorZero();
    const DirectX::XMVECTOR target = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);

    if (!cam.SetLookAt(eye, target, up))
    {
        std::printf("  FAIL: SetLookAt(identity view) returned false\n");
        return false;
    }

    // Aspect обязан совпадать с эталоном proj (16/9), иначе сравнение
    // бессмысленно: камеру по умолчанию имеет aspect = 1.0.
    cam.SetAspect(16.0f / 9.0f);

    DirectX::XMFLOAT4X4 vp;
    cam.BuildViewProjection(vp);

    const DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    // BuildViewProjection возвращает ТРАНСПОНИРОВАННУЮ матрицу — сравнивать
    // тоже с транспонированным proj.
    DirectX::XMFLOAT4X4 p;
    DirectX::XMStoreFloat4x4(&p, DirectX::XMMatrixTranspose(proj));

    if (!MatricesNear(vp, p))
    {
        std::printf("  FAIL: Identity * proj != proj\n");
        return false;
    }

    std::printf("  ok:   Identity * proj == proj\n");
    return true;
}

// Точка входа для CPU-тестов. Возвращает true, если все проверки прошли.
// Вызывается из отдельного тестового исполняемого файла, без GPU.
bool TestCameraMath()
{
    std::printf("\n[CameraMath] Deterministic CPU checks (no GPU)\n");

    bool ok = true;
    ok = TestPureProjection() && ok;
    ok = TestComposedViewProjection() && ok;
    ok = TestSetLookAtRejections() && ok;
    ok = TestSetAspectRejections() && ok;
    ok = TestIdentityView() && ok;

    std::printf("[CameraMath] %s\n", ok ? "ALL PASSED" : "FAILED");
    return ok;
}
