#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// Logging goes both to stdout (console subsystem) and to clouds_v0.log beside
// the EXE. All strings are ASCII/UTF-8; the Win32 title uses UTF-16 separately.
void InitLogging();
void Log(const char* format, ...);
void LogWarn(const char* format, ...);
void LogError(const char* format, ...);
std::filesystem::path LogFilePath();

// Diagnostics that must never unwind a caller's GPU resource owners. They
// attempt the normal logger but swallow every exception, including logger
// faults injected in the isolated fault build.
void LogBestEffort(const char* format, ...) noexcept;
void LogWarnBestEffort(const char* format, ...) noexcept;
void LogErrorBestEffort(const char* format, ...) noexcept;

// Low-level terminal boundary. TerminalFailClosed performs only a test-only
// atomic witness store and std::_Exit: no pipe/file I/O, no debugger call,
// no mutex, no heap. It never blocks on a diagnostic sink.
[[noreturn]] void TerminalFailClosed(const char* reason,
                                     const char* detail = nullptr,
                                     const char* detail2 = nullptr) noexcept;

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::filesystem::path ExecutableDirectory();
std::filesystem::path ShaderDirectory();
std::string HrToString(HRESULT hr);

[[noreturn]] void Fail(const std::string& message);
void ThrowIfFailed(HRESULT hr, const char* context);

D3D12_RESOURCE_BARRIER MakeTransition(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after);

void TransitionResource(
    ID3D12GraphicsCommandList* list,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after);

void InsertUavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource);

ComPtr<ID3DBlob> CompileShaderFile(
    const std::filesystem::path& file,
    const char* entryPoint,
    const char* target);

UINT DispatchGroupCount(UINT threadCount, UINT groupSize);
