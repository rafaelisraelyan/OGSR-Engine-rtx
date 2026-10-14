#include "stdafx.h"
#include "CHW12.h"
#include <Windows.h>

static const wchar_t* kWindowClass = L"XRDX12Smoke";
static const wchar_t* kWindowTitle = L"OGSR DX12 Smoke Test";

// F2.1-B: окно остаётся единственной точкой входа, но цикл теперь обрабатывает
// WM_SIZE (реальный вызов Resize) и немедленно выходит при отказе RenderFrame,
// вместо бесконечного горячего цикла на сломанном устройстве.
static CHW12* g_hw = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_hw)
        {
            const uint32_t w = LOWORD(lParam);
            const uint32_t h = HIWORD(lParam);
            if (!g_hw->Resize(w, h))
            {
                DestroyWindow(hwnd);
                return 0;
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
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc))
        return 1;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, kWindowTitle,
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        UnregisterClassW(kWindowClass, hInstance);
        return 1;
    }

    CHW12 hw;
    g_hw = &hw;

    if (!hw.Initialize(hwnd, 1280, 720))
    {
        wprintf(L"[smoke] CHW12::Initialize failed\n");
        DestroyWindow(hwnd);
        g_hw = nullptr;
        UnregisterClassW(kWindowClass, hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = {};
    bool running = true;
    while (running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                running = false;
                break;
            }
        }

        if (!running)
            break;

        if (!hw.RenderFrame())
        {
            wprintf(L"[smoke] RenderFrame failed, device removed=%d, stopping\n",
                    hw.IsDeviceRemoved() ? 1 : 0);
            running = false;
            break;
        }
    }

    hw.Shutdown();
    g_hw = nullptr;
    UnregisterClassW(kWindowClass, hInstance);

    return 0;
}
