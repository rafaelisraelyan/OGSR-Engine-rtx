// F4.0-B3-A: subprocess tests for the terminal GPU lifetime policy.
//
// Ordinary unit tests cannot observe destruction that happens after main()
// returns, so every scenario runs in a child process of this same executable.
// The parent checks the child exit code and the markers the child wrote.
//
// No D3D12 device is created here: the owner is a fake with an instrumented
// destructor, a COM-like object with a Release marker, a real Win32 event and
// a static-destruction probe. This proves the ownership policy, not real GPU
// failure recovery.
//
// Two observations are deliberately indirect-free:
//   - handle_close is written only after UniqueHandle actually closed the real
//     event and GetHandleInformation confirmed the value is no longer valid;
//   - FakeCom objects are created only on explicit request, so default-
//     constructed retention slots hold nothing and move assignment releases
//     the destination's previous object exactly once.

#include "b3a_gpu_state.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
fs::path g_markerDirectory;

// Lifecycle accounting. Markers are appended, so the parent can count exact
// occurrences instead of only checking existence.
long g_comCreated = 0;
long g_comReleased = 0;
long g_handleClosed = 0;
long g_handleCloseFailed = 0;

void Marker(const char* name)
{
    if (g_markerDirectory.empty() || name == nullptr || name[0] == '\0')
        return;
    std::ofstream file(g_markerDirectory / name, std::ios::binary | std::ios::app);
    file << "1\n";
}

long MarkerCount(const fs::path& directory, const char* name)
{
    std::ifstream file(directory / name, std::ios::binary);
    if (!file)
        return 0;
    long count = 0;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty())
            ++count;
    }
    return count;
}

void WriteAccounting(const char* tag)
{
    if (g_markerDirectory.empty())
        return;
    std::ofstream file(g_markerDirectory / "accounting.txt", std::ios::binary);
    file << "tag=" << (tag ? tag : "") << "\n";
    file << "com_created=" << g_comCreated << "\n";
    file << "com_released=" << g_comReleased << "\n";
    file << "handle_closed=" << g_handleClosed << "\n";
    file << "handle_close_failed=" << g_handleCloseFailed << "\n";
}

struct Probe
{
    const char* name = "";
    bool armed = false;
    ~Probe()
    {
        if (armed && name != nullptr && name[0] != '\0')
            Marker(name);
    }
};

// Minimal COM-like object: the final Release writes the marker.
struct FakeCom
{
    ULONG refs = 1;

    FakeCom() noexcept { ++g_comCreated; }

    void AddRef() noexcept { ++refs; }

    void Release() noexcept
    {
        if (--refs == 0)
        {
            ++g_comReleased;
            Marker("com_release");
            delete this;
        }
    }
};

// Owns the real Win32 event and turns "the handle is gone" into an observation:
// UniqueHandle::reset performs the actual CloseHandle, then GetHandleInformation
// must report the stored value as invalid.
class HandleProbe
{
public:
    HandleProbe() noexcept = default;
    HandleProbe(const HandleProbe&) = delete;
    HandleProbe& operator=(const HandleProbe&) = delete;

    HandleProbe(HandleProbe&& other) noexcept
        : handle_(std::move(other.handle_)),
          armed_(std::exchange(other.armed_, false))
    {
    }

    HandleProbe& operator=(HandleProbe&& other) noexcept
    {
        if (this != &other)
        {
            CloseAndVerify();
            handle_ = std::move(other.handle_);
            armed_ = std::exchange(other.armed_, false);
        }
        return *this;
    }

    ~HandleProbe() { CloseAndVerify(); }

    void OwnEvent()
    {
        CloseAndVerify();
        handle_.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
        armed_ = true;
    }

    HANDLE get() const noexcept { return handle_.get(); }
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // Closes through UniqueHandle and verifies the OS no longer knows the value.
    void CloseAndVerify() noexcept
    {
        const HANDLE raw = handle_.get();
        handle_.reset();
        if (raw == nullptr || raw == INVALID_HANDLE_VALUE)
            return;
        DWORD flags = 0;
        if (::GetHandleInformation(raw, &flags) != FALSE)
        {
            ++g_handleCloseFailed;
            Marker("handle_close_failed");
            return;
        }
        if (::GetLastError() != ERROR_INVALID_HANDLE)
        {
            ++g_handleCloseFailed;
            Marker("handle_close_failed");
            return;
        }
        ++g_handleClosed;
        if (armed_)
            Marker("handle_close");
    }

private:
    b3a::UniqueHandle handle_;
    bool armed_ = true;
};

struct FakeOwner
{
    Probe destructorProbe{"destructor"};
    FakeCom* com = nullptr;
    HandleProbe handle;
    b3a::GpuWorkState workState = b3a::GpuWorkState::NoWorkSubmitted;

    FakeOwner() = default;
    FakeOwner(const FakeOwner&) = delete;
    FakeOwner& operator=(const FakeOwner&) = delete;

    FakeOwner(FakeOwner&& other) noexcept
        : destructorProbe{other.destructorProbe.name, other.destructorProbe.armed},
          com(std::exchange(other.com, nullptr)),
          handle(std::move(other.handle)),
          workState(other.workState)
    {
        // The moved-from owner must not write lifecycle markers.
        other.destructorProbe.armed = false;
    }

    FakeOwner& operator=(FakeOwner&& other) noexcept
    {
        if (this != &other)
        {
            ReleaseCom();
            destructorProbe.name = other.destructorProbe.name;
            destructorProbe.armed = other.destructorProbe.armed;
            com = std::exchange(other.com, nullptr);
            handle = std::move(other.handle);
            workState = other.workState;
            other.destructorProbe.armed = false;
        }
        return *this;
    }

    ~FakeOwner() { ReleaseCom(); }

    // Explicit creation: empty slots and default-constructed owners allocate
    // nothing and emit no lifecycle markers until they are initialized.
    void OwnCom()
    {
        destructorProbe.armed = true;
        if (com == nullptr)
            com = new FakeCom();
    }

    void OwnEvent()
    {
        destructorProbe.armed = true;
        handle.OwnEvent();
    }

private:
    void ReleaseCom() noexcept
    {
        if (com != nullptr)
        {
            com->Release();
            com = nullptr;
        }
    }
};

struct TerminalAction
{
    int exitCode = 91;
    const char* accountingTag = nullptr;

    void operator()() noexcept
    {
        WriteAccounting(accountingTag);
        Marker("terminal");
        b3a::TerminalExit(exitCode);
    }
};

struct ThrowingTerminalAction
{
    void operator()()
    {
        WriteAccounting("J_throwing_terminal_action");
        Marker("terminal");
        throw std::runtime_error("terminal action threw");
    }
};

int ChildA_NegativeControl()
{
    // Negative control: the old static retention store is destroyed at static
    // destruction, which is exactly the defect the terminal path removes.
    static b3a::RetentionSlots<FakeOwner, 4> slots;
    static Probe staticProbe{"static_destruction", true};
    FakeOwner owner;
    owner.OwnCom();
    owner.OwnEvent();
    owner.workState = b3a::GpuWorkState::WorkSubmittedUnknown;
    if (!slots.TryPark(std::move(owner)))
        return 3;
    staticProbe.name = "static_destruction";
    return 0;
}

int ChildB_NormalCleanup()
{
    {
        FakeOwner owner;
        owner.OwnCom();
        owner.OwnEvent();
        b3a::UnknownCompletionGuard<TerminalAction> guard(
            owner.workState, TerminalAction{91, "B_normal_cleanup_no_work"});
        (void)guard;
    }
    WriteAccounting("B_normal_cleanup_no_work");
    return 0;
}

int ChildC_ConfirmedCompletion()
{
    {
        FakeOwner owner;
        owner.OwnCom();
        owner.OwnEvent();
        owner.workState = b3a::GpuWorkState::WorkCompleted;
        b3a::UnknownCompletionGuard<TerminalAction> guard(
            owner.workState, TerminalAction{91, "C_confirmed_completion"});
        (void)guard;
    }
    WriteAccounting("C_confirmed_completion");
    return 0;
}

int ChildD_UnknownCompletion()
{
    static Probe staticProbe{"static_destruction", true};
    FakeOwner owner;
    owner.OwnCom();
    owner.OwnEvent();
    owner.workState = b3a::GpuWorkState::WorkSubmittedUnknown;
    b3a::UnknownCompletionGuard<TerminalAction> guard(
        owner.workState, TerminalAction{91, "D_unknown_completion"});
    (void)guard;
    staticProbe.name = "static_destruction";
    return 0;
}

int ChildE_ExceptionAtUnknown()
{
    static Probe staticProbe{"static_destruction", true};
    FakeOwner owner;
    owner.OwnCom();
    owner.OwnEvent();
    owner.workState = b3a::GpuWorkState::WorkSubmittedUnknown;
    b3a::UnknownCompletionGuard<TerminalAction> guard(
        owner.workState, TerminalAction{91, "E_exception_at_unknown"});
    (void)guard;
    throw std::runtime_error("cpu fault after submit");
}

int ChildF_ExceptionBeforeSubmit()
{
    try
    {
        FakeOwner owner;
        owner.OwnCom();
        owner.OwnEvent();
        b3a::UnknownCompletionGuard<TerminalAction> guard(
            owner.workState, TerminalAction{91, "F_exception_before_submit"});
        (void)guard;
        throw std::runtime_error("cpu fault before submit");
    }
    catch (const std::exception&)
    {
    }
    WriteAccounting("F_exception_before_submit");
    return 30;
}

int ChildG_DeviceRemoved()
{
    static Probe staticProbe{"static_destruction", true};
    FakeOwner owner;
    owner.OwnCom();
    owner.OwnEvent();
    owner.workState = b3a::GpuWorkState::DeviceRemoved;
    b3a::UnknownCompletionGuard<TerminalAction> guard(
        owner.workState, TerminalAction{91, "G_device_removed"});
    (void)guard;
    staticProbe.name = "static_destruction";
    return 0;
}

int ChildH_RetentionExhausted()
{
    b3a::RetentionSlots<FakeOwner, 4> slots;
    for (int i = 0; i < 4; ++i)
    {
        FakeOwner owner;
        owner.OwnCom();
        owner.OwnEvent();
        if (!slots.TryPark(std::move(owner)))
            return 4;
    }
    FakeOwner overflow;
    overflow.OwnCom();
    overflow.OwnEvent();
    if (slots.TryPark(std::move(overflow)))
        return 5;
    // The store reports failure; the standalone policy is a terminal exit, and
    // the un-parked owner stays alive until the process ends.
    WriteAccounting("H_retention_exhausted");
    Marker("terminal");
    b3a::TerminalExit(92);
}

int ChildI_FailedClose()
{
    {
        FakeOwner owner;
        owner.OwnCom();
        owner.OwnEvent();
        const b3a::ListState list = b3a::ListStateAfterFailedClose();
        if (b3a::MaySubmit(list))
            return 6;
        if (b3a::MayResetList(list, owner.workState))
            return 7;
        b3a::UnknownCompletionGuard<TerminalAction> guard(
            owner.workState, TerminalAction{91, "I_failed_close"});
        (void)guard;
    }
    WriteAccounting("I_failed_close");
    return 0;
}

int ChildJ_ThrowingTerminalAction()
{
    static Probe staticProbe{"static_destruction", true};
    FakeOwner owner;
    owner.OwnCom();
    owner.OwnEvent();
    owner.workState = b3a::GpuWorkState::WorkSubmittedUnknown;
    b3a::UnknownCompletionGuard<ThrowingTerminalAction> guard(
        owner.workState, ThrowingTerminalAction{});
    (void)guard;
    staticProbe.name = "static_destruction";
    return 0;
}

int RunChild(const char* caseName, const fs::path& directory)
{
    g_markerDirectory = directory;
    if (std::strcmp(caseName, "A") == 0)
        return ChildA_NegativeControl();
    if (std::strcmp(caseName, "B") == 0)
        return ChildB_NormalCleanup();
    if (std::strcmp(caseName, "C") == 0)
        return ChildC_ConfirmedCompletion();
    if (std::strcmp(caseName, "D") == 0)
        return ChildD_UnknownCompletion();
    if (std::strcmp(caseName, "E") == 0)
        return ChildE_ExceptionAtUnknown();
    if (std::strcmp(caseName, "F") == 0)
        return ChildF_ExceptionBeforeSubmit();
    if (std::strcmp(caseName, "G") == 0)
        return ChildG_DeviceRemoved();
    if (std::strcmp(caseName, "H") == 0)
        return ChildH_RetentionExhausted();
    if (std::strcmp(caseName, "I") == 0)
        return ChildI_FailedClose();
    if (std::strcmp(caseName, "J") == 0)
        return ChildJ_ThrowingTerminalAction();
    return 10;
}

// ---------------------------------------------------------------------------
// In-process ownership model tests (no markers, exact counters).
// ---------------------------------------------------------------------------

int g_modelFailures = 0;

void ExpectModel(bool condition, const char* what)
{
    if (!condition)
    {
        ++g_modelFailures;
        std::printf("FAIL [ownership_model] %s\n", what);
    }
}

void TestFakeComOwnership()
{
    const long createdBefore = g_comCreated;
    const long releasedBefore = g_comReleased;

    {
        FakeOwner empty;
        ExpectModel(g_comCreated == createdBefore,
                    "default-constructed owner allocates no FakeCom");
        ExpectModel(empty.com == nullptr, "default-constructed owner is empty");

        empty.OwnCom();
        ExpectModel(g_comCreated == createdBefore + 1, "OwnCom creates exactly one");
        ExpectModel(empty.com != nullptr, "OwnCom installs the object");

        FakeOwner moved(std::move(empty));
        ExpectModel(empty.com == nullptr, "move constructor empties the source");
        ExpectModel(moved.com != nullptr, "move constructor transfers the object");
        ExpectModel(g_comReleased == releasedBefore,
                    "move constructor does not release");

        FakeOwner destination;
        destination.OwnCom();
        ExpectModel(g_comCreated == createdBefore + 2,
                    "destination creates its own object");
        destination = std::move(moved);
        ExpectModel(g_comReleased == releasedBefore + 1,
                    "move assignment releases the previous destination object");
        ExpectModel(moved.com == nullptr, "move assignment empties the source");
        ExpectModel(destination.com != nullptr, "move assignment transfers the object");

        FakeOwner& alias = destination;
        destination = std::move(alias);
        ExpectModel(g_comReleased == releasedBefore + 1,
                    "self-move releases nothing");
        ExpectModel(destination.com != nullptr, "self-move keeps ownership");

        moved.OwnCom();
        ExpectModel(g_comCreated == createdBefore + 3,
                    "moved-from owner can own a new object");
    }

    ExpectModel(g_comReleased == createdBefore + 3,
                "destructors release every remaining object exactly once");
    ExpectModel(g_comReleased == g_comCreated,
                "created and released counts balance");
    std::printf("PASS ownership_model com_created=%ld com_released=%ld\n",
                g_comCreated, g_comReleased);
}

void TestHandleProbeModel()
{
    const long closedBefore = g_handleClosed;
    const long failedBefore = g_handleCloseFailed;
    HANDLE raw = nullptr;
    {
        HandleProbe probe;
        probe.OwnEvent();
        raw = probe.get();
        ExpectModel(raw != nullptr && raw != INVALID_HANDLE_VALUE,
                    "probe owns a real event");
        DWORD flags = 0;
        ExpectModel(::GetHandleInformation(raw, &flags) != FALSE,
                    "handle is valid before destruction");

        HandleProbe moved(std::move(probe));
        ExpectModel(!probe, "moved-from probe owns no handle");
        ExpectModel(moved.get() == raw, "move transfers the handle");
    }
    DWORD flags = 0;
    ExpectModel(::GetHandleInformation(raw, &flags) == FALSE,
                "handle is invalid after probe destruction");
    ExpectModel(g_handleClosed == closedBefore + 1,
                "exactly one verified handle close");
    ExpectModel(g_handleCloseFailed == failedBefore,
                "no failed handle close");
    std::printf("PASS handle_probe_model closed=%ld failed=%ld\n",
                g_handleClosed, g_handleCloseFailed);
}

struct MarkerExpectation
{
    const char* name = "";
    long count = 0;
};

struct CaseResult
{
    const char* name = "";
    DWORD expectedExit = 0;
    std::vector<MarkerExpectation> markers;
    bool checkAccounting = false;
    long expectedCreated = 0;
    long expectedReleased = 0;
};

int g_failures = 0;
int g_cases = 0;

void Report(bool ok, const char* caseName, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL [%s] %s\n", caseName, what);
    }
}

bool ReadAccounting(const fs::path& directory, long& created, long& released)
{
    std::ifstream file(directory / "accounting.txt", std::ios::binary);
    if (!file)
        return false;
    std::string line;
    bool haveCreated = false;
    bool haveReleased = false;
    while (std::getline(file, line))
    {
        if (line.rfind("com_created=", 0) == 0)
        {
            created = std::strtol(line.c_str() + 12, nullptr, 10);
            haveCreated = true;
        }
        else if (line.rfind("com_released=", 0) == 0)
        {
            released = std::strtol(line.c_str() + 13, nullptr, 10);
            haveReleased = true;
        }
    }
    return haveCreated && haveReleased;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0)
    {
        try
        {
            return RunChild(argv[2], fs::path(argc >= 4 ? argv[3] : ""));
        }
        catch (const std::exception& error)
        {
            std::printf("child exception: %s\n", error.what());
            return 30;
        }
        catch (...)
        {
            std::printf("child unknown exception\n");
            return 30;
        }
    }

    std::printf("B3A_TERMINAL_LIFETIME_TESTS\n");
    TestFakeComOwnership();
    TestHandleProbeModel();

    wchar_t modulePath[MAX_PATH * 4] = {};
    const DWORD moduleLength = ::GetModuleFileNameW(
        nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (moduleLength == 0 ||
        static_cast<size_t>(moduleLength) == std::size(modulePath))
    {
        std::printf("FAIL cannot resolve own module path\n");
        return 2;
    }

    const fs::path root = fs::temp_directory_path() / "ogsr-b3a-terminal-lifetime";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec)
    {
        std::printf("FAIL cannot create marker root\n");
        return 2;
    }

    const CaseResult cases[] = {
        {"A_negative_control_static_destruction", 0,
         {{"destructor", 1}, {"com_release", 1}, {"handle_close", 1},
          {"static_destruction", 1}, {"terminal", 0}},
         false, 0, 0},
        {"B_normal_cleanup_no_work", 0,
         {{"destructor", 1}, {"com_release", 1}, {"handle_close", 1},
          {"static_destruction", 0}, {"terminal", 0}},
         true, 1, 1},
        {"C_confirmed_completion", 0,
         {{"destructor", 1}, {"com_release", 1}, {"handle_close", 1},
          {"static_destruction", 0}, {"terminal", 0}},
         true, 1, 1},
        {"D_unknown_completion", 91,
         {{"destructor", 0}, {"com_release", 0}, {"handle_close", 0},
          {"static_destruction", 0}, {"terminal", 1}},
         true, 1, 0},
        {"E_exception_at_unknown", 91,
         {{"destructor", 0}, {"com_release", 0}, {"handle_close", 0},
          {"static_destruction", 0}, {"terminal", 1}},
         true, 1, 0},
        {"F_exception_before_submit", 30,
         {{"destructor", 1}, {"com_release", 1}, {"handle_close", 1},
          {"static_destruction", 0}, {"terminal", 0}},
         true, 1, 1},
        {"G_device_removed", 91,
         {{"destructor", 0}, {"com_release", 0}, {"handle_close", 0},
          {"static_destruction", 0}, {"terminal", 1}},
         true, 1, 0},
        {"H_retention_exhausted", 92,
         {{"destructor", 0}, {"com_release", 0}, {"handle_close", 0},
          {"static_destruction", 0}, {"terminal", 1}},
         true, 5, 0},
        {"I_failed_close", 0,
         {{"destructor", 1}, {"com_release", 1}, {"handle_close", 1},
          {"static_destruction", 0}, {"terminal", 0}},
         true, 1, 1},
        {"J_throwing_terminal_action", b3a::kTerminalGpuExitCode,
         {{"destructor", 0}, {"com_release", 0}, {"handle_close", 0},
          {"static_destruction", 0}, {"terminal", 1}},
         true, 1, 0},
    };

    for (const CaseResult& item : cases)
    {
        ++g_cases;
        const fs::path directory = root / item.name;
        fs::create_directories(directory, ec);

        char narrowPath[MAX_PATH * 4] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, modulePath, -1, narrowPath,
                              static_cast<int>(std::size(narrowPath)), nullptr,
                              nullptr);
        std::string commandLine = "\"";
        commandLine += narrowPath;
        commandLine += "\" --child ";
        commandLine += item.name[0];
        commandLine += " \"";
        commandLine += directory.string();
        commandLine += "\"";

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back('\0');
        const BOOL created = ::CreateProcessA(
            nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
            nullptr, &startup, &process);
        if (!created)
        {
            Report(false, item.name, "CreateProcess failed");
            continue;
        }
        ::WaitForSingleObject(process.hProcess, 60000);
        DWORD exitCode = 0;
        ::GetExitCodeProcess(process.hProcess, &exitCode);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);

        Report(exitCode == item.expectedExit, item.name, "unexpected exit code");
        for (const MarkerExpectation& marker : item.markers)
        {
            const long actual = MarkerCount(directory, marker.name);
            if (actual != marker.count)
            {
                std::printf("FAIL [%s] marker %s expected %ld got %ld\n",
                            item.name, marker.name, marker.count, actual);
                ++g_failures;
            }
        }
        if (item.checkAccounting)
        {
            long createdCount = -1;
            long releasedCount = -1;
            const bool have = ReadAccounting(directory, createdCount, releasedCount);
            Report(have, item.name, "missing accounting");
            if (have)
            {
                if (createdCount != item.expectedCreated ||
                    releasedCount != item.expectedReleased)
                {
                    std::printf(
                        "FAIL [%s] accounting expected created=%ld released=%ld "
                        "got created=%ld released=%ld\n",
                        item.name, item.expectedCreated, item.expectedReleased,
                        createdCount, releasedCount);
                    ++g_failures;
                }
            }
        }
        std::printf("PASS %s exit=%lu\n", item.name,
                    static_cast<unsigned long>(exitCode));
    }

    std::printf("TERMINAL_SUMMARY cases=%d model_failures=%d failures=%d\n",
                g_cases, g_modelFailures, g_failures);
    return (g_failures == 0 && g_modelFailures == 0) ? 0 : 1;
}
