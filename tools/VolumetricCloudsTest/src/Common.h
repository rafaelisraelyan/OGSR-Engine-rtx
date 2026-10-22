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
