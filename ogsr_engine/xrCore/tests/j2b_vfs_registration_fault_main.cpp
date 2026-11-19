#include "xrCore.h"
#include "GameDx12J2ABatch.h"

#include "FS.h"
#include "LocatorAPI_defs.h"

#include "tests/j2b_vfs_fault.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>

bool OnMainThread() { return true; }
void LogD3D11DebugMessages() {}

namespace
{
int g_failed = 0;

void Check(const char* name, bool condition)
{
    std::printf("VFS_FAULT_CASE %-46s status=%s\n", name,
                condition ? "PASS" : "FAIL");
    if (!condition)
        ++g_failed;
}

bool HasFileActivity(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--file-activity") == 0)
            return true;
    }
    return false;
}

DWORD CurrentHandleCount()
{
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

bool OpenLevelOnce()
{
    IReader* reader = FS.r_open(fsgame::level, fsgame::level_files::level);
    if (reader == nullptr)
        return false;
    FS.r_close(reader);
    return reader == nullptr;
}

bool FaultOpenThrows(const char* expected)
{
    try
    {
        IReader* reader =
            FS.r_open(fsgame::level, fsgame::level_files::level_geom);
        if (reader != nullptr)
            FS.r_close(reader);
    }
    catch (const std::bad_alloc&)
    {
        return std::strcmp(expected, "bad_alloc") == 0;
    }
    catch (const std::runtime_error&)
    {
        return std::strcmp(expected, "runtime_error") == 0;
    }
    catch (...)
    {
        return false;
    }
    return false;
}
} // namespace

int main(int argc, char** argv)
{
    std::printf("VFS_FAULT_START\n");
    std::fflush(stdout);
    Core._initialize("m1_vfs_registration_fault", nullptr, TRUE,
                     "fsgame.ltx");
    const bool fileActivity = HasFileActivity(argc, argv);
    if (fileActivity)
        FS.m_Flags.set(CLocatorAPI::flDumpFileActivity, TRUE);
    FS_Path* levelPath = FS.get_path(fsgame::level);
    if (levelPath == nullptr)
    {
        std::printf("VFS_REGISTRATION_FAULT_RESULT FAIL level-path-missing\n");
        Core._destroy();
        return 1;
    }
    char ownerLevel[] = "jupiter";
    levelPath->_set(ownerLevel);

    std::printf("VFS_FAULT file_activity=%d\n", fileActivity ? 1 : 0);

    const uint32_t activeBefore = j2b_vfs_fault::ActiveReaderCount();
    const uint32_t slotsBefore = j2b_vfs_fault::OpenFileSlots();
    const DWORD handlesBefore = CurrentHandleCount();

    Check("baseline open/close", OpenLevelOnce());
    Check("baseline active readers balanced",
          j2b_vfs_fault::ActiveReaderCount() == activeBefore);
    const uint32_t slotsAfterBaseline = j2b_vfs_fault::OpenFileSlots();
    std::printf("VFS_FAULT baseline active=%u slots_before=%u "
                "slots_after=%u handles_before=%lu\n",
                activeBefore, slotsBefore, slotsAfterBaseline,
                static_cast<unsigned long>(handlesBefore));

    if (fileActivity)
    {
        j2b_vfs_fault::SetFailureMode(j2b_vfs_fault::FailureMode::BadAlloc);
        Check("registration bad_alloc thrown", FaultOpenThrows("bad_alloc"));
        Check("bad_alloc leaves no active reader",
              j2b_vfs_fault::ActiveReaderCount() == activeBefore);
        Check("bad_alloc leaves no published slot",
              j2b_vfs_fault::OpenFileSlots() == slotsAfterBaseline);
        j2b_vfs_fault::ClearFailure();

        j2b_vfs_fault::SetFailureMode(
            j2b_vfs_fault::FailureMode::PersistentBadAlloc);
        bool persistent = true;
        for (int i = 0; i < 3; ++i)
            persistent &= FaultOpenThrows("bad_alloc");
        Check("persistent bad_alloc throws every time", persistent);
        Check("persistent bad_alloc leaves no active reader",
              j2b_vfs_fault::ActiveReaderCount() == activeBefore);
        Check("persistent bad_alloc leaves no published slot",
              j2b_vfs_fault::OpenFileSlots() == slotsAfterBaseline);
        j2b_vfs_fault::ClearFailure();

        j2b_vfs_fault::SetFailureMode(
            j2b_vfs_fault::FailureMode::RuntimeError);
        Check("registration runtime_error thrown",
              FaultOpenThrows("runtime_error"));
        Check("runtime_error leaves no active reader",
              j2b_vfs_fault::ActiveReaderCount() == activeBefore);
        j2b_vfs_fault::ClearFailure();

        j2b_vfs_fault::SetFailureMode(j2b_vfs_fault::FailureMode::BadAlloc);
        j2bbatch12::Snapshot12 snapshot;
        j2bbatch12::ErrorInfo12 error;
        const bool adapterOk = GameDx12J2ABatch_ReadSnapshot(
            "jupiter", {2, 3}, snapshot, j2bbatch12::Limits12{}, error);
        Check("adapter returns false on VFS open fault", !adapterOk);
        Check("adapter reports VfsAllocation",
              error.code == j2bbatch12::Error12::VfsAllocation);
        Check("adapter does not publish partial snapshot",
              snapshot.levelBytes.empty() && snapshot.geomBytes.empty());
        Check("adapter leaves no active reader",
              j2b_vfs_fault::ActiveReaderCount() == activeBefore);
        j2b_vfs_fault::ClearFailure();

        Check("re-entry after fault succeeds", OpenLevelOnce());
        Check("re-entry leaves no active reader",
              j2b_vfs_fault::ActiveReaderCount() == activeBefore);

        for (int i = 0; i < 10; ++i)
        {
            j2b_vfs_fault::SetFailureMode(
                j2b_vfs_fault::FailureMode::BadAlloc);
            FaultOpenThrows("bad_alloc");
            j2b_vfs_fault::ClearFailure();
        }
        const DWORD handlesAfter = CurrentHandleCount();
        std::printf("VFS_FAULT handles_after=%lu\n",
                    static_cast<unsigned long>(handlesAfter));
        Check("handle balance after repeated faults",
              handlesAfter <= handlesBefore + 2);
    }
    else
    {
        // Without -file_activity the registration hook must not be called.
        j2b_vfs_fault::SetFailureMode(j2b_vfs_fault::FailureMode::BadAlloc);
        Check("file_activity off ignores registration hook", OpenLevelOnce());
        j2b_vfs_fault::ClearFailure();
        Check("file_activity off leaves no active reader",
              j2b_vfs_fault::ActiveReaderCount() == activeBefore);
    }

    Core._destroy();
    std::printf("VFS_REGISTRATION_FAULT_RESULT %s failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_failed);
    return g_failed == 0 ? 0 : 1;
}
