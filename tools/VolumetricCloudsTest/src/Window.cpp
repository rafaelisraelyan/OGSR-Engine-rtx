#include "Window.h"

#include "Common.h"

#include <utility>

namespace
{
constexpr wchar_t kWindowClass[] = L"OGSRCloudsTestV0Window";
bool g_classRegistered = false;
} // namespace

Window::Window() = default;

Window::~Window()
{
    Destroy();
}

bool Window::Create(
    const wchar_t* title,
    UINT clientWidth,
    UINT clientHeight,
    EventFn eventFn)
{
    if (hwnd_)
        return true;

    eventFn_ = std::move(eventFn);
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    if (!g_classRegistered)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = StaticWindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass))
            return false;
        g_classRegistered = true;
    }

    RECT rect{ 0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight) };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    hwnd_ = CreateWindowExW(
        0, kWindowClass, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, this);

    if (!hwnd_)
        return false;

    RECT client{};
    GetClientRect(hwnd_, &client);
    clientWidth_ = static_cast<UINT>(client.right - client.left);
    clientHeight_ = static_cast<UINT>(client.bottom - client.top);
    pendingWidth_ = clientWidth_;
    pendingHeight_ = clientHeight_;
    currentTitle_ = title ? title : L"";
    return true;
}

void Window::Destroy()
{
    if (hwnd_)
    {
        const HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(hwnd);
    }
}

bool Window::PumpMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
            return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

void Window::Show(int command)
{
    if (hwnd_)
        ShowWindow(hwnd_, command);
}

void Window::Close()
{
    if (hwnd_)
        DestroyWindow(hwnd_);
}

void Window::SetTitle(const std::wstring& title)
{
    if (!hwnd_ || title == currentTitle_)
        return;
    currentTitle_ = title;
    SetWindowTextW(hwnd_, title.c_str());
}

bool Window::ConsumeResize(UINT& width, UINT& height)
{
    if (!resizePending_)
        return false;
    resizePending_ = false;
    width = pendingWidth_;
    height = pendingHeight_;
    return width != 0 && height != 0;
}

LRESULT CALLBACK Window::StaticWindowProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* self = reinterpret_cast<Window*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
        return self->WindowProc(hwnd, message, wparam, lparam);
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Window::WindowProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_SIZE:
    {
        const UINT width = LOWORD(lparam);
        const UINT height = HIWORD(lparam);
        minimized_ = (wparam == SIZE_MINIMIZED);
        clientWidth_ = width;
        clientHeight_ = height;
        if (!minimized_ && width > 0 && height > 0)
        {
            pendingWidth_ = width;
            pendingHeight_ = height;
            resizePending_ = true;
        }
        return 0;
    }
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = 480;
        info->ptMinTrackSize.y = 320;
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (eventFn_)
            eventFn_(message, wparam, lparam);
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        if (eventFn_)
            eventFn_(message, wparam, lparam);
        return 0;
    case WM_KEYDOWN:
        if (eventFn_)
            eventFn_(message, wparam, lparam);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
