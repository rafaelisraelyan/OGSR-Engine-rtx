#include "stdafx.h"
#include "CHW12.h"
#include "dx12log.h"
#include <Windows.h>

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

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_hw)
        {
            const bool wasMinimized = (wParam == SIZE_MINIMIZED);
            SetRenderEnabled(!wasMinimized);

            // При минимизации client area равен нулю: Resize не вызываем.
            if (!wasMinimized)
            {
                const uint32_t w = LOWORD(lParam);
                const uint32_t h = HIWORD(lParam);
                if (w != 0 && h != 0 && !g_hw->Resize(w, h))
                {
                    dx12log::Write("[smoke] Resize(%u,%u) failed, stopping", w, h);
                    g_running = false;
                    DestroyWindow(hwnd);
                }
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    dx12log::Open();
    dx12log::Write("[smoke] xrDX12Smoke starting");

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

    if (!hw12.Initialize(hwnd, clientWidth, clientHeight))
    {
        dx12log::Write("[smoke] CHW12::Initialize failed, see [DX12] lines above for the stage");
        DestroyWindow(hwnd);
        g_hw = nullptr;
        UnregisterClassW(kWindowClass, hInstance);
        dx12log::Close();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (g_running)
    {
        // Минимизировано: GPU-рендеринг остановлен, но PeekMessage в пустой
        // очереди вернулся бы сразу — получался busy loop со 100% CPU на одном
        // ядре. WaitMessage блокируется до появления сообщения (в том числе
        // WM_SIZE при восстановлении окна), поэтому CPU простаивает, а
        // приложение остаётся отзывчивым. WM_QUIT тоже является сообщением
        // и разблокирует ожидание.
        if (g_minimized)
        {
            WaitMessage();
            continue;
        }

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

        if (!g_running)
            break;

        if (!hw12.RenderFrame())
        {
            dx12log::Write("[smoke] RenderFrame failed, device removed=%d, stopping",
                           hw12.IsDeviceRemoved() ? 1 : 0);
            g_running = false;
            break;
        }
    }

    hw12.Shutdown();
    g_hw = nullptr;
    UnregisterClassW(kWindowClass, hInstance);

    dx12log::Write("[smoke] exit");
    dx12log::Close();
    return 0;
}
