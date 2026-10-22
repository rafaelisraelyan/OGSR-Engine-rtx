#pragma once

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <string>

// Thin Win32 window wrapper. It owns the HWND and translates resize/minimize
// messages into state that the render loop can consume synchronously.
class Window
{
public:
    using EventFn = std::function<void(UINT message, WPARAM wparam, LPARAM lparam)>;

    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const wchar_t* title, UINT clientWidth, UINT clientHeight, EventFn eventFn);
    void Destroy();

    // Returns false when WM_QUIT was received.
    bool PumpMessages();

    void Show(int command);
    void Close();
    void SetTitle(const std::wstring& title);

    bool ConsumeResize(UINT& width, UINT& height);
    bool IsMinimized() const { return minimized_; }
    UINT ClientWidth() const { return clientWidth_; }
    UINT ClientHeight() const { return clientHeight_; }
    HWND Handle() const { return hwnd_; }
    bool IsValid() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message,
                                             WPARAM wparam, LPARAM lparam);
    LRESULT WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    UINT clientWidth_ = 0;
    UINT clientHeight_ = 0;
    bool minimized_ = false;
    bool resizePending_ = false;
    UINT pendingWidth_ = 0;
    UINT pendingHeight_ = 0;
    std::wstring currentTitle_;
    EventFn eventFn_;
};
