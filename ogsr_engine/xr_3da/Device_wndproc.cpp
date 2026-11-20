#include "stdafx.h"
#include "GameDx12Bridge.h"

bool CRenderDevice::on_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
    switch (uMsg)
    {
    case WM_SYSKEYDOWN: {
        return true;
    }
    case WM_ACTIVATE: {
        OnWM_Activate(wParam, lParam);
        return (false);
    }
    case WM_SETCURSOR: {
        result = 1;
        return (true);
    }
    case WM_SYSCOMMAND: {
        // Prevent moving/sizing and power loss in fullscreen mode
        switch (wParam)
        {
        case SC_SIZE:
        case SC_MOVE:
        case SC_MAXIMIZE:
            if (GameDx12Bridge_UsesNativeViewport())
                return false;
            result = 1;
            return true;
        case SC_MONITORPOWER: result = 1; return (true);
        }
        return (false);
    }
    case WM_CLOSE: {
        if (GameDx12Bridge_UsesNativeViewport())
        {
            Engine.Event.Defer("KERNEL:disconnect");
            Engine.Event.Defer("KERNEL:quit");
        }
        result = 0;
        return (true);
    }
    }

    return (false);
}
//-----------------------------------------------------------------------------
// Name: WndProc()
// Desc: Static msg handler which passes messages to the application class.
//-----------------------------------------------------------------------------
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    extern bool is_editor_active;
    if (is_editor_active && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    LRESULT result;
    if (Device.on_message(hWnd, uMsg, wParam, lParam, result))
        return (result);

    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
