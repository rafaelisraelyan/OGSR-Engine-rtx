// Test-only parent harness for R4 terminal safety.
//
// It creates a pre-initialized shared-memory witness, optionally fills
// stdout/stderr pipes before launching the child, enforces a hard timeout and
// records the child exit code plus the atomic witness event order. A child
// killed by the timeout is reported as timed_out=1; the parser treats that as
// a failure, never as a successful fail-closed exit.

#include "CloudWitness.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
struct Options
{
    std::wstring exe;
    std::wstring outDir;
    std::wstring name;
    DWORD timeoutMs = 60000;
    int blockMode = 0; // 1 stdout, 2 stderr, 3 both
    std::wstring blockAt;
    bool witnessDisabled = false;
    // Test-only: deliberately pass an invalid handle for this stream so the
    // child's pipe installation must FAIL and must not report activation.
    std::wstring breakHandle;
    std::vector<std::pair<std::wstring, std::wstring>> env;
    std::vector<std::wstring> childArgs;
};

struct PipeBlock
{
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    std::thread filler;
};

bool g_parentWitnessReady = false;
clouds::WitnessBlock* g_parentWitness = nullptr;
HANDLE g_parentMapping = nullptr;
bool g_parentPipeStatusReady = false;
clouds::PipeStatusBlock* g_parentPipeStatus = nullptr;
HANDLE g_parentPipeStatusMapping = nullptr;

const wchar_t* kTestEnvNames[] = {
    L"OGSR_CLOUD_FAULT_POINT",
    L"OGSR_CLOUD_FAULT_ARM_AT_START",
    L"OGSR_CLOUD_FAULT_ARM_AT_CLOUD_INIT",
    L"OGSR_CLOUD_FAULT_ARM_AT_PROBE_CAPACITY",
    L"OGSR_CLOUD_FAULT_STAGE",
    L"OGSR_CLOUD_FAULT_PROBE_CAPACITY",
    L"OGSR_CLOUD_LOGGER_FAULT",
    L"OGSR_CLOUD_LOGGER_FAULT_ARM",
    L"OGSR_CLOUD_WITNESS_SHM",
    L"OGSR_CLOUD_WITNESS_DISABLE",
    L"OGSR_CLOUD_TEST_SUPPRESS_LOGGING",
};

std::wstring JoinPath(const std::wstring& dir, const wchar_t* name)
{
    std::wstring result = dir;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
        result += L'\\';
    result += name;
    return result;
}

std::wstring QuoteArg(const std::wstring& value)
{
    std::wstring result = L"\"";
    for (wchar_t ch : value)
    {
        if (ch == L'"')
            result += L'\\';
        result += ch;
    }
    result += L'"';
    return result;
}

HANDLE CreateInheritableFile(const std::wstring& path)
{
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    return CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool CreateBlockedPipe(PipeBlock& block)
{
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&block.read, &block.write, &sa, 0))
        return false;
    SetHandleInformation(block.read, HANDLE_FLAG_INHERIT, 0);
    block.filler = std::thread([handle = block.write]
    {
        char buffer[4096]{};
        DWORD written = 0;
        while (WriteFile(handle, buffer, sizeof(buffer), &written, nullptr))
        {
        }
    });
    return true;
}

void ClosePipe(PipeBlock& block)
{
    if (block.read)
    {
        CloseHandle(block.read);
        block.read = nullptr;
    }
    if (block.filler.joinable())
        block.filler.join();
    if (block.write)
    {
        CloseHandle(block.write);
        block.write = nullptr;
    }
}

bool ParseOptions(int argc, wchar_t** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i];
        if (arg == L"--exe" && i + 1 < argc)
            options.exe = argv[++i];
        else if (arg == L"--out" && i + 1 < argc)
            options.outDir = argv[++i];
        else if (arg == L"--name" && i + 1 < argc)
            options.name = argv[++i];
        else if (arg == L"--timeout" && i + 1 < argc)
            options.timeoutMs = static_cast<DWORD>(_wtoi(argv[++i]));
        else if (arg == L"--block" && i + 1 < argc)
        {
            const std::wstring value = argv[++i];
            options.blockMode = value == L"stdout" ? 1
                : value == L"stderr" ? 2
                : value == L"both" ? 3 : 0;
        }
        else if (arg == L"--block-at" && i + 1 < argc)
            options.blockAt = argv[++i];
        else if (arg == L"--witness-disable")
            options.witnessDisabled = true;
        else if (arg == L"--break-handle" && i + 1 < argc)
            options.breakHandle = argv[++i];
        else if (arg == L"--env" && i + 1 < argc)
        {
            std::wstring pair = argv[++i];
            const size_t eq = pair.find(L'=');
            if (eq != std::wstring::npos)
                options.env.emplace_back(pair.substr(0, eq),
                                         pair.substr(eq + 1));
        }
        else if (arg == L"--")
        {
            for (++i; i < argc; ++i)
                options.childArgs.emplace_back(argv[i]);
            break;
        }
        else
        {
            fwprintf(stderr, L"unknown harness argument: %ls\n", arg.c_str());
            return false;
        }
    }
    return !options.exe.empty() && !options.outDir.empty() &&
           !options.name.empty();
}

void ClearTestEnv()
{
    for (const wchar_t* name : kTestEnvNames)
        SetEnvironmentVariableW(name, nullptr);
}

bool SetWitnessEnvironment(const Options& options)
{
    wchar_t shmName[180]{};
    swprintf_s(shmName, L"Local\\OGSRCloudWitness_%lu_%ls",
               GetCurrentProcessId(), options.name.c_str());
    const HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(clouds::WitnessBlock), shmName);
    if (!mapping)
        return false;
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                               sizeof(clouds::WitnessBlock));
    if (!view)
    {
        CloseHandle(mapping);
        return false;
    }
    g_parentMapping = mapping;

    auto* block = static_cast<clouds::WitnessBlock*>(view);
    ZeroMemory(block, sizeof(*block));
    block->magic = clouds::kWitnessMagic;
    block->version = clouds::kWitnessVersion;
    block->sequence = 0;
    block->terminalFlag = 0;
    g_parentWitness = block;
    g_parentWitnessReady = true;

    char narrow[180]{};
    WideCharToMultiByte(CP_UTF8, 0, shmName, -1, narrow, sizeof(narrow),
                        nullptr, nullptr);
    SetEnvironmentVariableA("OGSR_CLOUD_WITNESS_SHM", narrow);
    if (options.witnessDisabled)
        SetEnvironmentVariableA("OGSR_CLOUD_WITNESS_DISABLE", "1");

    // The pipe-status block is created unconditionally: it is the parent-side,
    // witness-independent proof that each requested stream was really installed.
    wchar_t pipeShmName[180]{};
    swprintf_s(pipeShmName, L"Local\\OGSRCloudPipeStatus_%lu_%ls",
               GetCurrentProcessId(), options.name.c_str());
    const HANDLE pipeMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(clouds::PipeStatusBlock), pipeShmName);
    if (!pipeMapping)
        return false;
    void* pipeView = MapViewOfFile(pipeMapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                   sizeof(clouds::PipeStatusBlock));
    if (!pipeView)
    {
        CloseHandle(pipeMapping);
        return false;
    }
    g_parentPipeStatusMapping = pipeMapping;
    auto* pipeBlock = static_cast<clouds::PipeStatusBlock*>(pipeView);
    ZeroMemory(pipeBlock, sizeof(*pipeBlock));
    pipeBlock->magic = clouds::kPipeStatusMagic;
    pipeBlock->version = clouds::kPipeStatusVersion;
    g_parentPipeStatus = pipeBlock;
    g_parentPipeStatusReady = true;

    char pipeNarrow[180]{};
    WideCharToMultiByte(CP_UTF8, 0, pipeShmName, -1, pipeNarrow,
                        sizeof(pipeNarrow), nullptr, nullptr);
    SetEnvironmentVariableA("OGSR_CLOUD_PIPESTATUS_SHM", pipeNarrow);
    return true;
}

void WritePipeStatus(const std::wstring& path, const Options& options)
{
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"w, ccs=UTF-8");
    if (!file)
        return;
    const clouds::PipeStatusBlock* block = g_parentPipeStatus;
    const int stdoutRequested = (options.blockMode & 1) ? 1 : 0;
    const int stderrRequested = (options.blockMode & 2) ? 1 : 0;
    fwprintf(file,
             L"{\"stdout_requested\":%d,\"stderr_requested\":%d,"
             L"\"stdout_ok\":%d,\"stderr_ok\":%d,\"activated\":%d,"
             L"\"probe_target\":%d,\"probe_started\":%d,"
             L"\"deactivated\":%d,\"stdout_error\":%d,\"stderr_error\":%d}\n",
             stdoutRequested, stderrRequested,
             block ? block->stdoutOk : 0,
             block ? block->stderrOk : 0,
             block ? block->activated : 0,
             block ? block->probeTarget : 0,
             block ? block->probeStarted : 0,
             block ? block->deactivated : 0,
             block ? block->stdoutError : 0,
             block ? block->stderrError : 0);
    fclose(file);
}

void WriteWitness(const std::wstring& path, const clouds::WitnessBlock* block)
{
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"w, ccs=UTF-8");
    if (!file)
        return;
    if (!block)
    {
        fwprintf(file, L"{\"terminal\":0,\"events\":[]}\n");
        fclose(file);
        return;
    }
    fwprintf(file, L"{\"terminal\":%ld,"
                   L"\"submit_count\":%lld,"
                   L"\"last_submitted_list\":%lld,"
                   L"\"last_fence_value\":%lld,"
                   L"\"last_completed_value\":%lld,"
                   L"\"fence_confirmed_count\":%ld,"
                   L"\"diag_defer_count\":%ld,"
                   L"\"diag_flush_count\":%ld,"
                   L"\"diag_unsafe_count\":%ld,"
                   L"\"inflight_at_defer\":%ld,"
                   L"\"inflight_at_check\":%ld,"
                   L"\"frame_submit_count\":%ld,"
                   L"\"frame_confirm_count\":%ld,"
                   L"\"gate_inserted\":%ld,"
                   L"\"gate_released\":%ld,"
                   L"\"events\":[",
             block->terminalFlag ? 1L : 0L,
             static_cast<long long>(block->submitCount),
             static_cast<long long>(block->lastSubmittedList),
             static_cast<long long>(block->lastFenceValue),
             static_cast<long long>(block->lastCompletedValue),
             block->fenceConfirmedCount ? 1L : 0L,
             block->diagDeferCount,
             block->diagFlushCount,
             block->diagUnsafeCount,
             block->inflightAtDefer,
             block->inflightAtCheck,
             block->frameSubmitCount,
             block->frameConfirmCount,
             block->gateInserted,
             block->gateReleased);
    const LONG count = block->sequence;
    bool first = true;
    for (LONG i = 0; i < count; ++i)
    {
        const clouds::WitnessEventEntry& entry =
            block->events[static_cast<size_t>(i) %
                          clouds::kWitnessEventCapacity];
        if (entry.sequence != i + 1)
            continue;
        const auto code = static_cast<clouds::WitnessCode>(entry.code);
        fwprintf(file, L"%ls{\"seq\":%ld,\"code\":%ld,\"name\":\"%hs\"}",
                 first ? L"" : L",", entry.sequence, entry.code,
                 clouds::WitnessCodeName(code));
        first = false;
    }
    fwprintf(file, L"]}\n");
    fclose(file);
}

int Run(const Options& options)
{
    CreateDirectoryW(options.outDir.c_str(), nullptr);
    ClearTestEnv();

    const std::wstring stdoutPath = JoinPath(options.outDir, L"stdout.txt");
    const std::wstring stderrPath = JoinPath(options.outDir, L"stderr.txt");
    const std::wstring exitPath = JoinPath(options.outDir, L"exit_code.txt");
    const std::wstring timeoutPath = JoinPath(options.outDir, L"timed_out.txt");
    const std::wstring witnessPath = JoinPath(options.outDir, L"witness.json");

    if (!SetWitnessEnvironment(options))
    {
        fwprintf(stderr, L"failed to create witness shared memory\n");
        return 2;
    }
    if (!options.witnessDisabled && options.blockMode != 0)
    {
        // Logging is deliberately NOT suppressed for blocked-pipe runs. If the
        // terminal critical interval contains any diagnostic sink, the child
        // must hang on the full pipe and be reported as a timeout, which the
        // parser counts as FAIL. Do not re-introduce automatic suppression
        // here: it would make the blocked-pipe evidence vacuous.
    }
    if (options.blockMode != 0 && options.blockAt.empty())
    {
        fwprintf(stderr, L"--block requires --block-at <label>\n");
        return 2;
    }
    for (const auto& [key, value] : options.env)
        SetEnvironmentVariableW(key.c_str(), value.c_str());

    HANDLE stdoutHandle = CreateInheritableFile(stdoutPath);
    if (stdoutHandle == INVALID_HANDLE_VALUE)
        return 2;
    HANDLE stderrHandle = CreateInheritableFile(stderrPath);
    if (stderrHandle == INVALID_HANDLE_VALUE)
        return 2;

    PipeBlock stdoutPipe;
    PipeBlock stderrPipe;
    const auto setHandleEnv = [](const wchar_t* name, HANDLE handle)
    {
        wchar_t value[32]{};
        swprintf_s(value, L"%llu",
                   static_cast<unsigned long long>(
                       reinterpret_cast<uintptr_t>(handle)));
        SetEnvironmentVariableW(name, value);
    };

    const bool breakStdout = options.breakHandle == L"stdout";
    const bool breakStderr = options.breakHandle == L"stderr";
    if (options.blockMode & 1)
    {
        if (!CreateBlockedPipe(stdoutPipe))
            return 2;
        setHandleEnv(L"OGSR_CLOUD_BLOCK_STDOUT_HANDLE", stdoutPipe.write);
        // Override AFTER the real handle: the child must detect the failure.
        if (breakStdout)
            SetEnvironmentVariableW(L"OGSR_CLOUD_BLOCK_STDOUT_HANDLE", L"1");
    }
    if (options.blockMode & 2)
    {
        if (!CreateBlockedPipe(stderrPipe))
            return 2;
        setHandleEnv(L"OGSR_CLOUD_BLOCK_STDERR_HANDLE", stderrPipe.write);
        if (breakStderr)
            SetEnvironmentVariableW(L"OGSR_CLOUD_BLOCK_STDERR_HANDLE", L"1");
    }
    if (options.blockMode != 0)
        SetEnvironmentVariableW(L"OGSR_CLOUD_BLOCK_AT_LABEL",
                                options.blockAt.c_str());

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE nulInput = CreateFileW(L"NUL", GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                  OPEN_EXISTING, 0, nullptr);
    if (nulInput == INVALID_HANDLE_VALUE)
        return 2;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nulInput;
    startup.hStdOutput = stdoutHandle;
    startup.hStdError = stderrHandle;

    std::wstring commandLine = QuoteArg(options.exe);
    for (const std::wstring& arg : options.childArgs)
        commandLine += L" " + QuoteArg(arg);

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(
        options.exe.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE, 0,
        nullptr, nullptr, &startup, &process);

    CloseHandle(nulInput);

    bool timedOut = false;
    DWORD exitCode = 0;
    if (launched)
    {
        const DWORD wait = WaitForSingleObject(process.hProcess, options.timeoutMs);
        timedOut = wait == WAIT_TIMEOUT;
        if (timedOut)
            TerminateProcess(process.hProcess, 0xDEAD);
        WaitForSingleObject(process.hProcess, 5000);
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    else
    {
        fwprintf(stderr, L"CreateProcess failed: %lu\n", GetLastError());
        exitCode = 0xFFFFFFFFu;
    }

    // Close read ends first so the pre-fill threads can leave WriteFile.
    ClosePipe(stdoutPipe);
    ClosePipe(stderrPipe);
    CloseHandle(stdoutHandle);
    CloseHandle(stderrHandle);

    // Re-use the parent mapping created before launch.
    WriteWitness(witnessPath,
                 g_parentWitnessReady ? g_parentWitness : nullptr);
    WritePipeStatus(JoinPath(options.outDir, L"pipe_status.json"), options);

    FILE* exitFile = nullptr;
    _wfopen_s(&exitFile, exitPath.c_str(), L"w, ccs=UTF-8");
    if (exitFile)
    {
        fwprintf(exitFile, L"%lu\n", exitCode);
        fclose(exitFile);
    }
    FILE* timeoutFile = nullptr;
    _wfopen_s(&timeoutFile, timeoutPath.c_str(), L"w, ccs=UTF-8");
    if (timeoutFile)
    {
        fwprintf(timeoutFile, L"%d\n", timedOut ? 1 : 0);
        fclose(timeoutFile);
    }
    return launched ? 0 : 2;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc >= 3 && std::wstring(argv[1]) == L"--write-env")
    {
        FILE* file = nullptr;
        _wfopen_s(&file, argv[2], L"w, ccs=UTF-8");
        if (file)
        {
            wchar_t value[32]{};
            GetEnvironmentVariableW(L"OGSR_CLOUD_TEST_SUPPRESS_LOGGING",
                                   value, 32);
            fwprintf(file, L"suppress=%ls\n", value);
            GetEnvironmentVariableW(L"OGSR_CLOUD_WITNESS_SHM", value, 32);
            fwprintf(file, L"shm=%ls\n", value);
            fclose(file);
        }
        return 0;
    }

    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        fwprintf(stderr,
                 L"usage: harness --exe <exe> --out <dir> --name <case> "
                 L"[--timeout ms] [--block none|stdout|stderr|both] "
                 L"[--witness-disable] [--env KEY=VALUE ...] -- <child args>\n");
        return 2;
    }
    return Run(options);
}
