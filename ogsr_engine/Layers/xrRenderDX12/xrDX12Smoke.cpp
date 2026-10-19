#include "stdafx.h"
#include "CHW12.h"
#include "dx12camera.h"
#include "dx12log.h"
#include "dx12cpuscene.h"
#include "gr/jobs/JobSystem.h"
#include "gr/jobs/JobConfig.h"
#include "gr/jobs/JobStats.h"
#include <Windows.h>
#include <vector>
#include <algorithm>
#include <exception>

static const wchar_t* kWindowClass = L"XRDX12Smoke";
static const wchar_t* kWindowTitle = L"OGSR DX12 Smoke Test";
static constexpr int kClientWidth = 1280;
static constexpr int kClientHeight = 720;

// F2.1-B.1: окно изменяемого размера с реальным client area. Resize вызывается
// только при изменении client area, не рекурсивно и не при минимизации.
static CHW12* g_hw = nullptr;
static bool g_running = true;
static bool g_minimized = false;

// Разрешает или запрещает рендеринг. Во время минимизации Present вернул бы
// DXGI_STATUS_OCCLUDED на каждом кадре, поэтому цикл просто простаивает.
static void SetRenderEnabled(bool enabled)
{
    g_minimized = !enabled;
}

// F3.1-DIAG: временная диагностика интерактивного resize.
// Не влияет на поведение: только логирование через QueryPerformanceCounter.
static LARGE_INTEGER g_diagFreq = {};
static LARGE_INTEGER g_diagLastFrame = {};
static LARGE_INTEGER g_diagLastHeartbeat = {};
static uint64_t g_diagFrameCount = 0;
static uint64_t g_diagResizeCount = 0;
static double g_diagMaxFramePauseMs = 0.0;

static double DiagElapsedMs(const LARGE_INTEGER& from, const LARGE_INTEGER& to)
{
    if (g_diagFreq.QuadPart == 0)
        return 0.0;
    return static_cast<double>(to.QuadPart - from.QuadPart) * 1000.0 /
           static_cast<double>(g_diagFreq.QuadPart);
}

// F3.1-FIX A: отложенный resize во время интерактивного изменения размеров.
// Во время WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE Windows генерирует десятки
// WM_SIZE. Каждый вызов Resize() делал полный GPU drain + ResizeBuffers +
// пересоздание ресурсов, что останавливало рендеринг.
// Теперь промежуточные WM_SIZE только запоминают последний размер, а один
// Resize() выполняется на WM_EXITSIZEMOVE (или на следующем кадре для
// WM_SIZE вне модального режима).
static bool g_inSizeMove = false;
static bool g_pendingResize = false;
static uint32_t g_pendingWidth = 0;
static uint32_t g_pendingHeight = 0;
static bool g_resizeInProgress = false;  // защита от реентерабельности

// F3.1-FIX B: рендеринг во время Win32 modal sizing loop.
// Во время перетаскивания границы Windows запускает свой message loop,
// и основной цикл WinMain не получает управления. WM_TIMER доставляется
// внутри модального цикла, что позволяет продолжать отрисовку.
static bool g_timerActive = false;
static uint64_t g_timerFrameCount = 0;
// F3.3.0a: RenderFrame вернул false в таймерном пути. Латчится до конца
// сессии: повторная отрисовка из WM_TIMER запрещена, а выход выполняется
// штатно через WM_CLOSE основным циклом. Renderer из callback не разрушаем.
static bool g_timerFrameFailed = false;

// F3.1-FIX C: непрерывность времени камеры.
// Камера использовала абсолютное время от старта процесса. После паузы
// в несколько секунд камера скачком переходила на новый угол.
// Теперь ведётся накопленное время с ограничением deltaTime.
static float g_cameraTime = 0.0f;
static constexpr float kMaxDeltaSeconds = 0.05f;  // 50 ms — не более 20 FPS эквивалент
static LARGE_INTEGER g_lastCameraTick = {};
static bool g_fixedPose = false;  // глобальный флаг для WndProc
static bool g_freeAspect = false;  // -freeaspect: отключает фиксацию 16:9
static RECT g_originalWindowRect = {};  // исходный window rect при WM_ENTERSIZEMOVE
static DWORD g_windowStyle = 0;
static DWORD g_windowExStyle = 0;

// F3.1-ASPECT: фиксация соотношения сторон 16:9 для client area при ручном
// изменении размера окна. Вычисления ведутся от исходного положения, а не
// последовательным округлением уже скорректированных размеров.
static constexpr float kAspectW = 16.0f;
static constexpr float kAspectH = 9.0f;
static constexpr int kMinClientWidth = 320;
static constexpr int kMinClientHeight = 180;

#include "dx12cpuruntime.inl"

// Вычисляет размер window rect из клиентского с учётом non-client области.
// Используется для корректного определения границ при WM_SIZING.
static RECT CalcWindowRectFromClient(const RECT& clientRect, DWORD style, DWORD exStyle, UINT dpi)
{
    RECT windowRect = clientRect;
    AdjustWindowRectExForDpi(&windowRect, style, FALSE, exStyle, dpi);
    return windowRect;
}

// Корректирует предлагаемый window rect для сохранения 16:9 client area.
// edge — одна из WMSZ_* констант, указывающая перетаскиваемую границу.
static void ApplyAspectRatioLock(RECT* pRect, int edge, const RECT& originalWindowRect,
                                  DWORD style, DWORD exStyle, UINT dpi)
{
    // Вычисляем non-client размеры из оригинального window rect.
    const int origWindowWidth = originalWindowRect.right - originalWindowRect.left;
    const int origWindowHeight = originalWindowRect.bottom - originalWindowRect.top;
    RECT origClient = originalWindowRect;
    AdjustWindowRectExForDpi(&origClient, style, FALSE, exStyle, dpi);
    const int nonClientWidth = origWindowWidth - (origClient.right - origClient.left);
    const int nonClientHeight = origWindowHeight - (origClient.bottom - origClient.top);

    // Определяем, какие стороны зафиксированы (противоположные перетаскиваемой).
    const bool isLeft = (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT);
    const bool isTop = (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT);

    // Вычисляем новые размеры client area на основе перетаскиваемой стороны.
    int newClientWidth, newClientHeight;

    if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT)
    {
        // Ширина определяется пользователем, высота = ширина * 9/16.
        newClientWidth = pRect->right - pRect->left - nonClientWidth;
        newClientHeight = static_cast<int>(static_cast<float>(newClientWidth) * kAspectH / kAspectW);
    }
    else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM)
    {
        // Высота определяется пользователем, ширина = высота * 16/9.
        newClientHeight = pRect->bottom - pRect->top - nonClientHeight;
        newClientWidth = static_cast<int>(static_cast<float>(newClientHeight) * kAspectW / kAspectH);
    }
    else
    {
        // Углы: используем большее измерение для определения обоих.
        const int deltaX = (isLeft) ? (originalWindowRect.left - pRect->left) : (pRect->right - originalWindowRect.right);
        const int deltaY = (isTop) ? (originalWindowRect.top - pRect->top) : (pRect->bottom - originalWindowRect.bottom);
        if (deltaX * kAspectH > deltaY * kAspectW)
        {
            newClientWidth = deltaX;
            newClientHeight = static_cast<int>(static_cast<float>(newClientWidth) * kAspectH / kAspectW);
        }
        else
        {
            newClientHeight = deltaY;
            newClientWidth = static_cast<int>(static_cast<float>(newClientHeight) * kAspectW / kAspectH);
        }
    }

    // Ограничение минимального размера.
    if (newClientWidth < kMinClientWidth)
    {
        newClientWidth = kMinClientWidth;
        newClientHeight = static_cast<int>(static_cast<float>(newClientWidth) * kAspectH / kAspectW);
    }
    if (newClientHeight < kMinClientHeight)
    {
        newClientHeight = kMinClientHeight;
        newClientWidth = static_cast<int>(static_cast<float>(newClientHeight) * kAspectW / kAspectH);
    }

    // Восстанавливаем window rect с зафиксированными сторонами.
    if (isLeft)
        pRect->left = originalWindowRect.left;
    else
        pRect->right = originalWindowRect.right;
    if (isTop)
        pRect->top = originalWindowRect.top;
    else
        pRect->bottom = originalWindowRect.bottom;

    // Применяем новые размеры к перетаскиваемой стороне.
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
        pRect->left = pRect->right - newClientWidth - nonClientWidth;
    else if (edge == WMSZ_RIGHT || edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMRIGHT)
        pRect->right = pRect->left + newClientWidth + nonClientWidth;

    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
        pRect->top = pRect->bottom - newClientHeight - nonClientHeight;
    else if (edge == WMSZ_BOTTOM || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT)
        pRect->bottom = pRect->top + newClientHeight + nonClientHeight;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_diagFreq.QuadPart == 0)
        QueryPerformanceFrequency(&g_diagFreq);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    switch (msg)
    {
    case WM_ENTERSIZEMOVE:
        dx12log::Write("[diag] WM_ENTERSIZEMOVE t=%.3f ms",
                       DiagElapsedMs(g_diagLastFrame, now));
        g_inSizeMove = true;
        g_pendingResize = false;
        g_diagResizeCount = 0;

        // F3.1-ASPECT: сохраняем исходный window rect и стили для вычислений
        // в WM_SIZING. Вычисления ведутся от исходного положения, а не
        // последовательным округлением уже скорректированных размеров.
        GetWindowRect(hwnd, &g_originalWindowRect);
        g_windowStyle = GetWindowLong(hwnd, GWL_STYLE);
        g_windowExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

        // F3.1-FIX B: запускаем таймер для рендеринга во время модального
        // sizing loop. WM_TIMER доставляется внутри модального цикла,
        // позволяя продолжать отрисовку, когда основной цикл WinMain
        // не получает управления.
        if (!g_timerActive)
        {
            SetTimer(hwnd, 1, 16, nullptr);  // ~60 FPS
            g_timerActive = true;
        }
        return 0;

    case WM_SIZING:
        // F3.1-ASPECT: корректируем предлагаемый window rect для сохранения
        // 16:9 client area. Вычисления ведутся от исходного положения.
        // Не вызываем Resize(), WaitForGpu(), ResizeBuffers() или RenderFrame().
        if (!g_freeAspect && g_hw)
        {
            RECT* pRect = reinterpret_cast<RECT*>(lParam);
            const UINT dpi = GetDpiForWindow(hwnd);
            ApplyAspectRatioLock(pRect, static_cast<int>(wParam), g_originalWindowRect,
                                 g_windowStyle, g_windowExStyle, dpi);
        }
        return 0;

    case WM_EXITSIZEMOVE:
        dx12log::Write("[diag] WM_EXITSIZEMOVE t=%.3f ms, resizeCount=%llu",
                       DiagElapsedMs(g_diagLastFrame, now),
                       static_cast<unsigned long long>(g_diagResizeCount));
        g_inSizeMove = false;

        // F3.1-FIX A: применяем накопленный размер одним Resize().
        if (g_pendingResize && g_hw && !g_resizeInProgress)
        {
            g_resizeInProgress = true;
            const uint32_t w = g_pendingWidth;
            const uint32_t h = g_pendingHeight;
            g_pendingResize = false;
            g_hw->Resize(w, h);
            if (h > 0) g_sceneAspect = static_cast<float>(w) / h;
            g_resizeInProgress = false;
        }
        g_diagResizeCount = 0;

        // F3.1-FIX B: останавливаем таймер — основной цикл снова активен.
        if (g_timerActive)
        {
            KillTimer(hwnd, 1);
            g_timerActive = false;
        }
        return 0;

    case WM_SIZE:
        if (g_hw)
        {
            const bool wasMinimized = (wParam == SIZE_MINIMIZED);
            SetRenderEnabled(!wasMinimized);

            if (!wasMinimized)
            {
                const uint32_t w = LOWORD(lParam);
                const uint32_t h = HIWORD(lParam);
                ++g_diagResizeCount;
                dx12log::Write("[diag] WM_SIZE %ux%u #%llu t=%.3f ms%s",
                               w, h,
                               static_cast<unsigned long long>(g_diagResizeCount),
                               DiagElapsedMs(g_diagLastFrame, now),
                               g_inSizeMove ? " (deferred)" : "");

                // F3.1-FIX A: во время модального sizing loop только
                // запоминаем последний размер. Resize() будет вызван
                // на WM_EXITSIZEMOVE или на следующем кадре.
                if (g_inSizeMove)
                {
                    if (w != 0 && h != 0)
                    {
                        g_pendingWidth = w;
                        g_pendingHeight = h;
                        g_pendingResize = true;
                    }
                }
                else if (w != 0 && h != 0 && !g_resizeInProgress)
                {
                    // WM_SIZE вне модального режима: отложенный resize
                    // до следующего кадра через флаг.
                    if (w != g_pendingWidth || h != g_pendingHeight)
                    {
                        g_pendingWidth = w;
                        g_pendingHeight = h;
                        g_pendingResize = true;
                    }
                }
            }
            else
            {
                dx12log::Write("[diag] WM_SIZE SIZE_MINIMIZED t=%.3f ms",
                               DiagElapsedMs(g_diagLastFrame, now));
                g_pendingResize = false;
            }
        }
        return 0;

    case WM_TIMER:
        // F3.1-FIX B: рендеринг во время модального sizing loop.
        // Таймер сбрасывается каждый кадр, чтобы избежать накопления.
        if (g_hw && !g_minimized && g_hw->IsInitialized() &&
            !g_resizeInProgress && msg == WM_TIMER && wParam == 1)
        {
            // Не рисуем, если окно не видно или идёт Shutdown.
            // F3.3.0a: g_timerFrameFailed запрещает повторную отрисовку после
            // уже зафиксированного сбоя в этом же пути.
            if (!g_running || g_timerFrameFailed)
                return 0;

            // F3.1-FIX C: обновляем время камеры с ограничением deltaTime.
            if (!g_fixedPose)
            {
                LARGE_INTEGER tick;
                QueryPerformanceCounter(&tick);
                if (g_lastCameraTick.QuadPart != 0)
                {
                    const float delta = static_cast<float>(tick.QuadPart - g_lastCameraTick.QuadPart) /
                                        static_cast<float>(g_diagFreq.QuadPart);
                    const float clamped = (delta > kMaxDeltaSeconds) ? kMaxDeltaSeconds : delta;
                    g_cameraTime += clamped;
                }
                g_lastCameraTick = tick;
                g_hw->UpdateCamera(g_cameraTime);
            }

            // F3.3.0a: RenderFrame возвращает false не только при device
            // removal, но и по причинам, добавленным в F3.3.0 (сбой подготовки
            // кадра, защита от реентерабельности). Молчаливый игнор оставлял
            // таймер крутящимся без единого кадра и без диагностики.
            //
            // Renderer из callback не разрушаем: останавливаем таймер,
            // запрещаем дальнейшую отрисовку из WM_TIMER и просим основной
            // цикл выйти штатно через WM_CLOSE (DestroyWindow -> PostQuitMessage
            // -> Shutdown на потоке-владельце).
            if (!g_hw->RenderFrameFromTimer())
            {
                dx12log::Write("[diag] WM_TIMER: RenderFrame failed, device removed=%d, "
                               "preparation failed=%d; stopping timer rendering",
                               g_hw->IsDeviceRemoved() ? 1 : 0,
                               g_hw->PreparationFailed() ? 1 : 0);
                g_timerFrameFailed = true;
                g_running = false;
                if (g_timerActive)
                {
                    KillTimer(hwnd, 1);
                    g_timerActive = false;
                }
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            ++g_timerFrameCount;
        }
        return 0;

    case WM_CLOSE:
        dx12log::Write("[diag] WM_CLOSE t=%.3f ms",
                       DiagElapsedMs(g_diagLastFrame, now));
        // F3.1-FIX B: останавливаем таймер перед уничтожением окна.
        if (g_timerActive)
        {
            KillTimer(hwnd, 1);
            g_timerActive = false;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        dx12log::Write("[diag] WM_DESTROY t=%.3f ms",
                       DiagElapsedMs(g_diagLastFrame, now));
        // F3.1-FIX B: на всякий случай останавливаем таймер.
        if (g_timerActive)
        {
            KillTimer(hwnd, 1);
            g_timerActive = false;
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    dx12log::Open();
    dx12log::Write("[smoke] xrDX12Smoke starting");
    gr::jobs::JobSystem jobSystem;
    g_jobSystem = &jobSystem;

    // F3.1: разбор аргументов командной строки без новых зависимостей.
    // -fixedpose — детерминированный ракурс для тестов.
    // -depthtest — автоматический depth-тест через readback.
    // -freeaspect — отключает фиксацию 16:9 (свободный resize).
    renderprep12::Mode prepMode=renderprep12::Mode::Serial;
    bool prepSelfTest=false;
    bool timerFault=false; // F3.3.0a TEST ONLY: одноразовый отказ WM_TIMER-пути
    uint32_t prepFrames=0;
    bool fixedPose = false;
    bool depthTest = false;
    bool badArgument = false;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                if (_wcsicmp(argv[i], L"-prepselftest") == 0) prepSelfTest=true;
                // F3.3.0a TEST ONLY: одноразовый отказ WM_TIMER-пути.
                else if (_wcsicmp(argv[i], L"-timerfault") == 0) timerFault=true;
                else if (_wcsicmp(argv[i], L"-prepframes") == 0) {
                    if (i+1<argc && _wtoi(argv[i+1])>0) prepFrames=static_cast<uint32_t>(_wtoi(argv[++i]));
                    else badArgument=true;
                }
                else if (_wcsicmp(argv[i], L"-renderprep") == 0) {
                    if (i+1>=argc) badArgument=true;
                    else { ++i;
                        if (!_wcsicmp(argv[i],L"serial")) prepMode=renderprep12::Mode::Serial;
                        else if (!_wcsicmp(argv[i],L"parallel")) prepMode=renderprep12::Mode::Parallel;
                        else if (!_wcsicmp(argv[i],L"verify")) prepMode=renderprep12::Mode::Verify;
                        else badArgument=true;
                    }
                }
                else if (_wcsicmp(argv[i], L"-fixedpose") == 0)
                    fixedPose = true;
                else if (_wcsicmp(argv[i], L"-depthtest") == 0)
                    depthTest = true;
                else if (_wcsicmp(argv[i], L"-freeaspect") == 0)
                    g_freeAspect = true;
                else if (_wcsicmp(argv[i], L"-cpuselftest") == 0)
                    g_cpuSelfTest = true;
                else if (_wcsicmp(argv[i], L"-resizetest") == 0)
                    g_resizeTest = true;
                // F3.2C: выбор пути подготовки CPU-сцены.
                else if (_wcsicmp(argv[i], L"-jobs") == 0)
                {
                    if (i + 1 < argc)
                    {
                        ++i;
                        if (_wcsicmp(argv[i], L"serial") == 0)
                            g_jobsParallel = false;
                        else if (_wcsicmp(argv[i], L"gr") == 0 || _wcsicmp(argv[i], L"parallel") == 0)
                            g_jobsParallel = true;
                        else
                        {
                            badArgument = true;
                            dx12log::Write("[smoke] invalid -jobs value '%ls' (expected serial|gr)",
                                           argv[i]);
                        }
                    }
                    else
                    {
                        badArgument = true;
                        dx12log::Write("[smoke] -jobs requires a value (serial|gr)");
                    }
                }
                else if (_wcsicmp(argv[i], L"-jobworkers") == 0)
                {
                    if (i + 1 < argc)
                    {
                        const int value = _wtoi(argv[++i]);
                        if (value < 0 || value > 64)
                        {
                            badArgument = true;
                            dx12log::Write("[smoke] invalid -jobworkers value %d (expected 0..64)", value);
                        }
                        else
                        {
                            g_jobWorkers = static_cast<uint32_t>(value);
                        }
                    }
                    else
                    {
                        badArgument = true;
                        dx12log::Write("[smoke] -jobworkers requires a value");
                    }
                }
                else if (_wcsicmp(argv[i], L"-jobgrain") == 0)
                {
                    if (i + 1 < argc)
                    {
                        const int value = _wtoi(argv[++i]);
                        if (value < 1 || value > 65536)
                        {
                            badArgument = true;
                            dx12log::Write("[smoke] invalid -jobgrain value %d (expected 1..65536)", value);
                        }
                        else
                        {
                            g_jobGrain = static_cast<uint32_t>(value);
                        }
                    }
                    else
                    {
                        badArgument = true;
                        dx12log::Write("[smoke] -jobgrain requires a value");
                    }
                }
                else if (_wcsicmp(argv[i], L"-stress") == 0)
                {
                    if (i + 1 < argc)
                    {
                        const int value = _wtoi(argv[++i]);
                        if (value < 0 || value > 65536)
                        {
                            badArgument = true;
                            dx12log::Write("[smoke] invalid -stress value %d (expected 0..65536)", value);
                        }
                        else
                        {
                            g_stressObjects = static_cast<uint32_t>(value);
                        }
                    }
                    else
                    {
                        badArgument = true;
                        dx12log::Write("[smoke] -stress requires a value");
                    }
                }
                else if (_wcsicmp(argv[i], L"-jobsverify") == 0)
                {
                    // Каждый кадр считать и _serial_, и _parallel_, и сравнивать
                    // результаты. Нужен для доказательства совпадения, а не для
                    // производительности.
                    g_cpuVerify = true;
                }
                else if (_wcsicmp(argv[i], L"-cputest") == 0 || _wcsicmp(argv[i], L"-cpubench") == 0)
                {
                    const bool verify = _wcsicmp(argv[i], L"-cputest") == 0;
                    // НЕинтерактивная самопроверка: прогнать N кадров после
                    // прогрева, сравнить Serial и Parallel и выйти с кодом
                    // совпадения. Требует -stress; если он не задан, берётся
                    // 1024 объекта.
                    if (i + 1 < argc)
                    {
                        const int value = _wtoi(argv[++i]);
                        if (value < 1 || value > 100000)
                        {
                            badArgument = true;
                            dx12log::Write("[smoke] invalid -cputest value %d (expected 1..100000)",
                                           value);
                        }
                        else
                        {
                            g_cpuTestFrames = static_cast<uint32_t>(value);
                            g_cpuVerify = g_cpuVerify || verify;
                            if (g_stressObjects == 0)
                            {
                                g_stressObjects = 1024;
                            }
                        }
                    }
                    else
                    {
                        badArgument = true;
                        dx12log::Write("[smoke] -cputest requires a value");
                    }
                }
            }
            LocalFree(argv);
        }
    }

    if (badArgument)
    {
        dx12log::Write("[smoke] FATAL: invalid command line argument, aborting");
        dx12log::Close();
        return 1;
    }
    if (prepSelfTest) { const bool ok=renderprep12::RunTests(); dx12log::Close(); return ok?0:1; }
    if (g_cpuSelfTest)
    {
        const bool passed = dx12cpuscene::RunSelfTests();
        dx12log::Close();
        return passed ? 0 : 2;
    }
    if (g_cpuTestFrames && !g_stressObjects) g_stressObjects = 1024;
    if (g_resizeTest && g_cpuTestFrames < 30)
    {
        dx12log::Write("[smoke] -resizetest requires -cputest/-cpubench >= 30");
        dx12log::Close();
        return 1;
    }

    // BLOCKER 4: при -depthtest камера должна оставаться неизменной до
    // завершения теста. Иначе UpdateCamera в цикле каждый кадр меняет
    // ракурс, и пиксели не воспроизводимы между прогонами.
    fixedPose = fixedPose || depthTest;
    g_fixedPose = fixedPose;

    dx12log::Write("[smoke] modes: fixedPose=%d, depthTest=%d, aspectLock=%s",
                   fixedPose ? 1 : 0, depthTest ? 1 : 0,
                   g_freeAspect ? "OFF" : "ON");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc))
    {
        dx12log::Write("[smoke] RegisterClassExW failed, GetLastError=%lu", GetLastError());
        dx12log::Close();
        return 1;
    }

    // AdjustWindowRectEx: 1280x720 — это CLIENT area, а не размер окна.
    // Без этой поправки backbuffer получал бы размер окна вместе со рамкой.
    RECT desired = {0, 0, kClientWidth, kClientHeight};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    if (!AdjustWindowRectEx(&desired, style, FALSE, 0))
    {
        dx12log::Write("[smoke] AdjustWindowRectEx failed, GetLastError=%lu", GetLastError());
        UnregisterClassW(kWindowClass, hInstance);
        dx12log::Close();
        return 1;
    }

    const int winWidth = desired.right - desired.left;
    const int winHeight = desired.bottom - desired.top;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, kWindowTitle, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        dx12log::Write("[smoke] CreateWindowExW failed, GetLastError=%lu", GetLastError());
        UnregisterClassW(kWindowClass, hInstance);
        dx12log::Close();
        return 1;
    }

    // Фактический client area: источник истины для размера backbuffer.
    RECT client = {};
    GetClientRect(hwnd, &client);
    const uint32_t clientWidth = static_cast<uint32_t>(client.right - client.left);
    const uint32_t clientHeight = static_cast<uint32_t>(client.bottom - client.top);
    dx12log::Write("[smoke] client area: %ux%u (window %dx%d)",
                   clientWidth, clientHeight, winWidth, winHeight);

    CHW12 hw12;
    g_hw = &hw12;

    // F3.1: настройка режимов тестирования.
    hw12.SetTestMode(depthTest);
    if (depthTest || fixedPose)
    {
        // Фиксированный ракурс для детерминированных тестов и сравнения
        // кадров между прогонами.
        DirectX::XMVECTOR eye, target, up;
        hw12.GetCameraFixedPose(eye, target, up);
        hw12.SetCameraLookAt(eye, target, up);
    }
    else
    {
        // Орбитальная камера по умолчанию: yaw растёт линейно по времени,
        // pitch фиксирован. Мировые координаты кубов не меняются — вращается
        // камера.
    }

    if (!hw12.Initialize(hwnd, clientWidth, clientHeight))
    {
        dx12log::Write("[smoke] CHW12::Initialize failed, see [DX12] lines above for the stage");
        DestroyWindow(hwnd);
        g_hw = nullptr;
        UnregisterClassW(kWindowClass, hInstance);
        dx12log::Close();
        return 1;
    }

    // F3.2C: один JobSystem на приложение. Initialize вызывается один раз,
    // Shutdown — один раз при выходе; в цикле кадра ничего не пересоздаётся.
    if (prepMode!=renderprep12::Mode::Serial || (g_stressObjects > 0 && (g_jobsParallel || g_cpuVerify)))
    {
        gr::jobs::JobConfig jobConfig;
        jobConfig.backgroundWorkers = g_jobWorkers;   // 0 = автоматическая политика
        jobConfig.automaticWorkers = true;
        jobConfig.defaultGrainSize = g_jobGrain;
        jobConfig.enableStats = true;

        if (!g_jobSystem->Initialize(jobConfig))
        {
            // Нельзя выдавать Serial/Serial за успешную проверку Parallel.
            dx12log::Write("[jobs] FATAL: initialization failed; use -jobs serial without verification");
            hw12.Shutdown();
            g_hw = nullptr;
            DestroyWindow(hwnd);
            UnregisterClassW(kWindowClass, hInstance);
            dx12log::Close();
            return 2;
        }
        else
        {
            g_jobSystemReady = true;
            ++g_jobStarts;
            const gr::jobs::JobStats stats = g_jobSystem->GetStats();
            dx12log::Write("[jobs] ready: backgroundWorkers=%u totalParticipants=%u "
                           "logicalProcessors=%u physicalCores=%u",
                           stats.backgroundWorkers, stats.totalParticipants,
                           stats.logicalProcessors, stats.physicalCores);
            if (g_jobsParallel && !g_jobsAvailable)
            {
                g_jobsParallel = false;
            }
        }
    }

    hw12.ConfigurePreparation(prepMode,g_jobSystemReady?g_jobSystem:nullptr);
    // F3.3.0a TEST ONLY: одноразовый отказ WM_TIMER-пути. Флаг виден только
    // RenderFrameFromTimer(); главный цикл вызывает RenderFrame() и потому
    // инъекцию не израсходует и не заметит.
    if (timerFault) hw12.ArmTimerRenderFailure();

    // F3.2C: таблица CPU-сцены строится один раз из фиксированного seed, поэтому
    // Serial и Parallel получают идентичные входные данные.
    if (g_stressObjects > 0)
    {
        CpuSceneState& scene = g_cpuScene;
        scene.objects.resize(g_stressObjects);
        scene.statesSerial.resize(g_stressObjects);
        scene.statesParallel.resize(g_stressObjects);
        scene.samples.reserve(g_cpuTestFrames);
        dx12cpuscene::BuildObjectTable(scene.objects.data(), g_stressObjects, 0xC0FFEEu);
        dx12log::Write("[cpu] stress scene: %u objects (seed=0xC0FFEE)", g_stressObjects);
    }

    if (g_cpuTestFrames > 0)
    {
        // Прогрев обязателен: первые кадры включают старт пула, прогрев кэшей и
        // GPU pipeline, и в статистику попадать не должны.
        g_cpuTestRemaining = g_cpuTestFrames;
        g_cpuWarmupFrames = 30;
        dx12log::Write("[cputest] frames=%u warmup=%u verify=%d",
                       g_cpuTestFrames, g_cpuWarmupFrames, g_cpuVerify ? 1 : 0);
    }

    // Фактическая конфигурация, а не запрошенная: видно, что применилось.
    dx12log::Write("[smoke] config: jobs=%s jobworkers=%u jobgrain=%u stress=%u verify=%d",
                   (g_jobsParallel && g_jobsAvailable) ? "gr" : "serial",
                   g_jobWorkers, g_jobGrain, g_stressObjects, g_cpuVerify ? 1 : 0);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // F3.1: -depthtest — автоматический GPU depth-test с самостоятельным
    // завершением. Рисует один кадр, синхронизирует GPU, проверяет пиксели
    // и возвращает exit code. Обычный Smoke сохраняет прежнее поведение.
    if (depthTest)
    {
        PrepareSceneCPU();
        if (!hw12.RenderFrame())
        {
            dx12log::Write("[smoke] -depthtest: RenderFrame failed, device removed=%d",
                           hw12.IsDeviceRemoved() ? 1 : 0);
            hw12.Shutdown();
            StopJobs();
            g_hw = nullptr;
            UnregisterClassW(kWindowClass, hInstance);
            dx12log::Write("[smoke] -depthtest: RenderFrame FAILED");
            dx12log::Close();
            return 1;
        }

        // VerifyDepthTest инкапсулирует GPU fence sync, CPU Map/Unmap и
        // проверку пикселей. mappedReadback не открывается наружу.
        CHW12::DepthTestResult testResult = hw12.VerifyDepthTest();

        if (testResult.passed)
        {
            dx12log::Write("[smoke] -depthtest: PASS");
        }
        else
        {
            dx12log::Write("[smoke] -depthtest: FAIL — %s", testResult.message);
        }

        hw12.Shutdown();
        ReportCpuScene();
        StopJobs();
        g_hw = nullptr;
        UnregisterClassW(kWindowClass, hInstance);
        dx12log::Write("[smoke] -depthtest: %s", testResult.passed ? "PASS" : "FAIL");
        dx12log::Close();
        return testResult.passed && g_cpuScene.mismatchCount == 0 ? 0 : 1;
    }

    MSG msg = {};
    int exitCode = 0;
    while (g_running)
    {
        // 1. Обрабатываем все накопленные сообщения. WM_SIZE с SIZE_MINIMIZED
        // устанавливает g_minimized здесь же, в WndProc.
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                g_running = false;
                break;
            }
        }

        // 2. WM_QUIT обработан: выходим, не рендеря лишний кадр.
        if (!g_running)
            break;

        // 3. Минимизировано: ждём сообщения. Ожидание идёт ПОСЛЕ полного
        // pump, иначе WM_SIZE/SIZE_MINIMIZED, уже лежащий в очереди, не был
        // бы обработан до блокировки. PeekMessage в пустой очереди вернулся
        // бы сразу, дав busy loop со 100% CPU; WaitMessage блокируется до
        // нового сообщения (в том числе WM_SIZE при восстановлении окна),
        // поэтому RenderFrame() не вызывается и дополнительный кадр после
        // SIZE_MINIMIZED невозможен. Sleep не используется.
        if (g_minimized)
        {
            WaitMessage();
            continue;
        }

        // 4. Окно видимо: рендерим.
        if (!g_minimized)
        {
            // F3.1-DIAG: heartbeat и измерение пауз между кадров.
            {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                ++g_diagFrameCount;
                if (g_diagLastFrame.QuadPart != 0)
                {
                    const double pauseMs = DiagElapsedMs(g_diagLastFrame, now);
                    if (pauseMs > g_diagMaxFramePauseMs)
                        g_diagMaxFramePauseMs = pauseMs;
                }
                g_diagLastFrame = now;

                if (g_diagLastHeartbeat.QuadPart == 0 ||
                    DiagElapsedMs(g_diagLastHeartbeat, now) >= 1000.0)
                {
                    g_diagLastHeartbeat = now;
                    dx12log::Write("[diag] heartbeat: frames=%llu, maxPause=%.1f ms",
                                   static_cast<unsigned long long>(g_diagFrameCount),
                                   g_diagMaxFramePauseMs);
                }
            }

            // F3.1-FIX A: обработка отложенного resize вне модального режима.
            if (g_pendingResize && !g_resizeInProgress)
            {
                g_resizeInProgress = true;
                const uint32_t w = g_pendingWidth;
                const uint32_t h = g_pendingHeight;
                g_pendingResize = false;
                if (!hw12.Resize(w, h))
                {
                    dx12log::Write("[smoke] Resize failed");
                    exitCode = 1;
                    break;
                }
                ++g_runtimeResizes;
                if (h > 0)
                {
                    // Аспект CPU-сцены держится в локальной копии: состояние
                    // CHW12 из этого потока не читается.
                    g_sceneAspect = static_cast<float>(w) / static_cast<float>(h);
                }
                g_resizeInProgress = false;
            }

            // F3.1-FIX C: орбитальная камера с deltaTime вместо абсолютного
            // времени. После паузы камера не скачком переходит на новый угол.
            if (!fixedPose)
            {
                LARGE_INTEGER tick;
                QueryPerformanceCounter(&tick);
                if (g_lastCameraTick.QuadPart != 0)
                {
                    const float delta = static_cast<float>(tick.QuadPart - g_lastCameraTick.QuadPart) /
                                        static_cast<float>(g_diagFreq.QuadPart);
                    const float clamped = (delta > kMaxDeltaSeconds) ? kMaxDeltaSeconds : delta;
                    g_cameraTime += clamped;
                }
                g_lastCameraTick = tick;
                hw12.UpdateCamera(g_cameraTime);
            }

            // F3.2C: подготовка CPU-данных сцены. Вызывается только в видимом
            // состоянии окна, поэтому при минимизации работа не выполняется.
            // Двухкубовый режим не затрагивается: при -stress 0 функция выходит
            // немедленно.
            try { PrepareSceneCPU(); }
            catch (const std::exception& error)
            {
                dx12log::Write("[cpu] FAIL: %s", error.what());
                exitCode = 2;
                break;
            }

            // F3.2C: неинтерактивная самопроверка — ограниченное число кадров
            // после прогрева, затем штатное закрытие окна.
            if (g_cpuTestFrames > 0)
            {
                if (g_cpuWarmupFrames > 0)
                {
                    --g_cpuWarmupFrames;
                }
                else if (g_cpuTestRemaining > 0)
                {
                    --g_cpuTestRemaining;
                }
                if (g_cpuWarmupFrames == 0 && g_cpuTestRemaining == 0)
                {
                    dx12log::Write("[cputest] completed %u measured frames", g_cpuTestFrames);
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            }

            if (!hw12.RenderFrame())
            {
                dx12log::Write("[smoke] RenderFrame failed, device removed=%d, stopping",
                               hw12.IsDeviceRemoved() ? 1 : 0);
                g_running = false;
                exitCode = 1;
                break;
            }
            if (prepFrames && --prepFrames==0) g_running=false;
            // Проверяем штатный WM_SIZE -> отложенный Resize без изменения GPU-кода.
            // F3.3.0a: третий resize намеренно НЕ 16:9. Прежние два размера
            // (960x540, 1280x720) сохраняют соотношение сторон, поэтому путь
            // смены аспекта камеры после resize (CHW12::SetAspect) в наборе
            // не проверялся вообще — ни одним прогоном.
            if (g_resizeTest && (g_cpuStep == 10 || g_cpuStep == 20 || g_cpuStep == 30))
            {
                RECT size = {0, 0,
                    g_cpuStep == 10 ? 960 : (g_cpuStep == 20 ? 1280 : 1024),
                    g_cpuStep == 10 ? 540 : (g_cpuStep == 20 ? 720 : 768)};
                AdjustWindowRectEx(&size, style, FALSE, 0);
                if (!SetWindowPos(hwnd, nullptr, 0, 0, size.right - size.left, size.bottom - size.top,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
                {
                    exitCode = 1;
                    break;
                }
            }
        }
    }

    if (hw12.PreparationFailed()) exitCode=1;
    // F3.3.0a: сбой отрисовки в таймерном пути не должен давать код возврата 0.
    if (g_timerFrameFailed) exitCode=1;
    hw12.Shutdown();
    g_hw = nullptr;

    // F3.2C: код выхода неинтерактивного теста отражает совпадение Serial/Parallel.
    if (g_cpuTestFrames > 0)
    {
        const auto stats = g_jobSystem->GetStats();
        const bool passed = exitCode == 0 && g_cpuScene.frames == g_cpuTestFrames &&
            (!g_cpuVerify || (g_cpuScene.verifyFrames == g_cpuTestFrames && g_jobSystemReady)) &&
            (!g_resizeTest || g_runtimeResizes >= 2) && g_cpuScene.mismatchCount == 0 &&
            stats.tasksSubmitted == stats.tasksCompleted && stats.jobFailures == 0 &&
            stats.unobservedFailures == 0;
        dx12log::Write("[cputest] %s: verifyFrames=%u mismatches=%llu maxDev=%.3g",
                       passed ? "PASS" : "FAIL", g_cpuScene.verifyFrames,
                       static_cast<unsigned long long>(g_cpuScene.mismatchCount),
                       g_cpuScene.maxDeviation);
        if (!passed)
        {
            exitCode = 2;
        }
    }

    // F3.2C: CPU-задания обязаны завершиться до остановки подсистемы. Shutdown()
    // дожидается всех незавершённых заданий, поэтому активные группы не могут
    // пережить этот вызов.
    ReportCpuScene();
    StopJobs();
    dx12log::Write("[runtime] rendered=%llu resizeSuccess=%u starts=%u timerFrames=%llu",
        (unsigned long long)g_diagFrameCount, g_runtimeResizes, g_jobStarts,
        (unsigned long long)g_timerFrameCount);

    UnregisterClassW(kWindowClass, hInstance);

    dx12log::Write("[smoke] exit, code=%d", exitCode);
    dx12log::Close();
    return exitCode;
}
