#include "Common.h"

#include <d3dcompiler.h>

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <time.h>

namespace
{
std::mutex g_logMutex;
std::ofstream g_logFile;
bool g_logInitialized = false;

std::filesystem::path ComputeLogPath()
{
    std::error_code ec;
    const auto directory = ExecutableDirectory();
    auto candidate = directory / L"clouds_v0.log";
    std::ofstream probe(candidate, std::ios::app);
    if (probe)
        return candidate;

    wchar_t temp[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    if (length > 0 && length < MAX_PATH)
        return std::filesystem::path(temp) / L"clouds_v0.log";
    return candidate;
}

void LogLine(const char* level, const char* format, va_list args)
{
    char body[4096]{};
    vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    char line[4352]{};
    snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u][%s] %s\n",
             now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, level, body);

    std::lock_guard<std::mutex> lock(g_logMutex);
    fputs(line, stdout);
    fflush(stdout);
    if (g_logFile)
    {
        g_logFile << line;
        g_logFile.flush();
    }
    OutputDebugStringA(line);
}
} // namespace

std::filesystem::path ExecutableDirectory()
{
    wchar_t module[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return std::filesystem::current_path();
    return std::filesystem::path(module).parent_path();
}

std::filesystem::path ShaderDirectory()
{
    return ExecutableDirectory() / L"shaders";
}

std::filesystem::path LogFilePath()
{
    static const std::filesystem::path path = ComputeLogPath();
    return path;
}

void InitLogging()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logInitialized)
        return;
    g_logInitialized = true;
    g_logFile.open(LogFilePath(), std::ios::out | std::ios::trunc);
    if (!g_logFile)
        fprintf(stderr, "warning: could not open log file %s\n", LogFilePath().string().c_str());
}

void Log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogLine("info", format, args);
    va_end(args);
}

void LogWarn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogLine("warn", format, args);
    va_end(args);
}

void LogError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogLine("error", format, args);
    va_end(args);
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::string HrToString(HRESULT hr)
{
    const char* name = nullptr;
    switch (hr)
    {
    case S_OK: name = "S_OK"; break;
    case E_FAIL: name = "E_FAIL"; break;
    case E_INVALIDARG: name = "E_INVALIDARG"; break;
    case E_OUTOFMEMORY: name = "E_OUTOFMEMORY"; break;
    case E_NOTIMPL: name = "E_NOTIMPL"; break;
    case E_NOINTERFACE: name = "E_NOINTERFACE"; break;
    case DXGI_ERROR_DEVICE_REMOVED: name = "DXGI_ERROR_DEVICE_REMOVED"; break;
    case DXGI_ERROR_DEVICE_HUNG: name = "DXGI_ERROR_DEVICE_HUNG"; break;
    case DXGI_ERROR_DEVICE_RESET: name = "DXGI_ERROR_DEVICE_RESET"; break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: name = "DXGI_ERROR_DRIVER_INTERNAL_ERROR"; break;
    case DXGI_ERROR_INVALID_CALL: name = "DXGI_ERROR_INVALID_CALL"; break;
    case DXGI_ERROR_WAS_STILL_DRAWING: name = "DXGI_ERROR_WAS_STILL_DRAWING"; break;
    case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: name = "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE"; break;
    case DXGI_STATUS_OCCLUDED: name = "DXGI_STATUS_OCCLUDED"; break;
    default: break;
    }

    char buffer[96]{};
    if (name)
        snprintf(buffer, sizeof(buffer), "%s (0x%08lX)", name,
                 static_cast<unsigned long>(hr));
    else
        snprintf(buffer, sizeof(buffer), "HRESULT 0x%08lX",
                 static_cast<unsigned long>(hr));
    return buffer;
}

void Fail(const std::string& message)
{
    LogError("%s", message.c_str());
    throw std::runtime_error(message);
}

void ThrowIfFailed(HRESULT hr, const char* context)
{
    if (SUCCEEDED(hr))
        return;
    std::ostringstream stream;
    stream << context << " failed: " << HrToString(hr);
    Fail(stream.str());
}

D3D12_RESOURCE_BARRIER MakeTransition(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

void TransitionResource(
    ID3D12GraphicsCommandList* list,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    if (before == after || !resource)
        return;
    const D3D12_RESOURCE_BARRIER barrier = MakeTransition(resource, before, after);
    list->ResourceBarrier(1, &barrier);
}

void InsertUavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    list->ResourceBarrier(1, &barrier);
}

ComPtr<ID3DBlob> CompileShaderFile(
    const std::filesystem::path& file,
    const char* entryPoint,
    const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> messages;
    const HRESULT hr = D3DCompileFromFile(
        file.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target, flags, 0,
        bytecode.GetAddressOf(), messages.GetAddressOf());

    if (messages && messages->GetBufferSize() > 0)
    {
        std::string text(static_cast<const char*>(messages->GetBufferPointer()),
                         messages->GetBufferSize());
        if (SUCCEEDED(hr))
            LogWarn("HLSL warnings (%s, %s):\n%s", entryPoint, target, text.c_str());
        else
            LogError("HLSL errors (%s, %s):\n%s", entryPoint, target, text.c_str());
    }

    if (FAILED(hr))
    {
        std::ostringstream stream;
        stream << "shader compilation failed: " << file.string()
               << " entry=" << entryPoint << " target=" << target
               << " (" << HrToString(hr) << ")";
        Fail(stream.str());
    }

    Log("shader compiled: %s [%s / %s] %zu bytes",
        file.filename().string().c_str(), entryPoint, target,
        static_cast<size_t>(bytecode->GetBufferSize()));
    return bytecode;
}

UINT DispatchGroupCount(UINT threadCount, UINT groupSize)
{
    return (threadCount + groupSize - 1) / groupSize;
}
