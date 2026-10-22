#include "Application.h"
#include "Common.h"

#include <Windows.h>

#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace
{
bool ParseUint32(const std::wstring& text, uint32_t& value)
{
    if (text.empty())
        return false;
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || errno == ERANGE ||
        parsed > 0xfffffffful)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseInt(const std::wstring& text, int& value)
{
    if (text.empty())
        return false;
    errno = 0;
    wchar_t* end = nullptr;
    const long parsed = wcstol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || errno == ERANGE)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

void PrintCliHelp()
{
    Log("OGSRCloudsTest - standalone D3D12 volumetric noise viewer (V0)");
    Log("");
    Log("Usage:");
    Log("  OGSRCloudsTest.exe [options]");
    Log("");
    Log("Options:");
    Log("  --size=64|128        initial 3D noise resolution (default 128)");
    Log("  --seed=N             deterministic noise seed (default 1337)");
    Log("  --format=16f|unorm   force the volume format (default: 16F, fallback UNORM)");
    Log("  --adapter=N          select DXGI adapter index from the startup list");
    Log("  --warp               force the Microsoft WARP software adapter");
    Log("  --debug              enable the D3D12 debug layer");
    Log("  --gpu-validation     enable GPU-based validation (implies --debug)");
    Log("  --test-no-validation test-only: simulate missing validation infrastructure");
    Log("  --test-no-info-queue test-only: hide ID3D12InfoQueue while debug layer is on");
    Log("  --no-vsync           disable vsync");
    Log("  --selftest           run readback/periodicity/determinism checks and exit");
    Log("  --capture[=DIR]      render BMP captures and exit (default: <exe>/artifacts/capture)");
    Log("  --frames=N           run N frames and exit (automation)");
    Log("  --resize-test        resize the window during --frames to test ResizeBuffers");
    Log("  --help               show this help");
    Log("");
    Log("Interactive controls: 1/2/3 channel, arrows/wheel Z slice, Space animation,");
    Log("D seam diagnostic, N regenerate, 4 toggle 64/128, G/F/O/W/B/E/K parameters,");
    Log("H/F1 help, Esc exit.");
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitLogging();

    Log("command line: %s", WideToUtf8(GetCommandLineW()).c_str());

    AppOptions options;
#ifdef _DEBUG
    options.renderer.debugLayer = true;
#endif

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];
        if (argument == L"--warp")
        {
            options.renderer.forceWarp = true;
        }
        else if (argument == L"--debug")
        {
            options.renderer.debugLayer = true;
        }
        else if (argument == L"--no-debug")
        {
            options.renderer.debugLayer = false;
            options.renderer.gpuValidation = false;
        }
        else if (argument == L"--gpu-validation")
        {
            options.renderer.debugLayer = true;
            options.renderer.gpuValidation = true;
        }
        else if (argument == L"--test-no-validation")
        {
            // Test-only hook: simulate missing debug/GPU validation
            // infrastructure while keeping the real GPU alive.
            options.renderer.testNoValidation = true;
        }
        else if (argument == L"--test-no-info-queue")
        {
            // Test-only hook: keep the debug layer but hide ID3D12InfoQueue.
            options.renderer.testNoInfoQueue = true;
        }
        else if (argument == L"--no-vsync")
        {
            options.renderer.vsync = false;
        }
        else if (argument == L"--selftest")
        {
            options.selfTest = true;
        }
        else if (argument == L"--resize-test")
        {
            options.resizeTest = true;
        }
        else if (argument == L"--help" || argument == L"-h" || argument == L"/?")
        {
            options.help = true;
        }
        else if (argument == L"--capture")
        {
            options.capture = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-')
                options.captureDir = argv[++i];
        }
        else if (argument.rfind(L"--capture=", 0) == 0)
        {
            options.capture = true;
            options.captureDir = argument.substr(10);
        }
        else if (argument.rfind(L"--size=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(7), value))
            {
                LogError("invalid --size value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.noise.resolution = value;
        }
        else if (argument.rfind(L"--seed=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(7), value))
            {
                LogError("invalid --seed value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.noise.seed = value;
        }
        else if (argument.rfind(L"--adapter=", 0) == 0)
        {
            int value = -1;
            if (!ParseInt(argument.substr(10), value) || value < 0)
            {
                LogError("invalid --adapter value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.renderer.adapterIndex = value;
        }
        else if (argument.rfind(L"--format=", 0) == 0)
        {
            const std::wstring format = argument.substr(9);
            if (format == L"16f")
                options.preferredFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            else if (format == L"unorm")
                options.preferredFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            else
            {
                LogError("invalid --format value: %s (expected 16f or unorm)",
                         WideToUtf8(format).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--frames=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(9), value) || value == 0)
            {
                LogError("invalid --frames value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.frames = value;
        }
        else
        {
            LogError("unknown argument: %s", WideToUtf8(argument).c_str());
            PrintCliHelp();
            return 2;
        }
    }

    if (options.help)
    {
        PrintCliHelp();
        return 0;
    }

    if (options.capture && options.captureDir.empty())
        options.captureDir = ExecutableDirectory() / L"artifacts" / L"capture";
    if (options.resizeTest && options.frames == 0)
        options.frames = 90;

    const std::string validation = options.noise.Validate();
    if (!validation.empty())
    {
        LogError("invalid noise parameters: %s", validation.c_str());
        return 2;
    }

    Log("options: size=%u seed=%u adapter=%d warp=%d debug=%d gpuValidation=%d testNoValidation=%d testNoInfoQueue=%d vsync=%d selftest=%d capture=%d frames=%u",
        options.noise.resolution, options.noise.seed,
        options.renderer.adapterIndex, options.renderer.forceWarp ? 1 : 0,
        options.renderer.debugLayer ? 1 : 0,
        options.renderer.gpuValidation ? 1 : 0,
        options.renderer.testNoValidation ? 1 : 0,
        options.renderer.testNoInfoQueue ? 1 : 0,
        options.renderer.vsync ? 1 : 0,
        options.selfTest ? 1 : 0, options.capture ? 1 : 0, options.frames);

    const bool showErrors = !options.selfTest && !options.capture &&
                            options.frames == 0;

    try
    {
        Application app(std::move(options));
        return app.Run();
    }
    catch (const std::exception& exception)
    {
        LogError("fatal error: %s", exception.what());
        if (showErrors)
        {
            const std::wstring message = Utf8ToWide(exception.what());
            MessageBoxW(nullptr, message.c_str(), L"OGSR Облака V0 — ошибка",
                        MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
