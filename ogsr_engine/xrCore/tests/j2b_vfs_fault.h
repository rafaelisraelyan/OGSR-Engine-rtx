#pragma once

#include <cstdint>

// Test-only VFS file-activity registration fault hook.
// This header is used only when OGSR_J2B_VFS_FAULT_INJECTION is defined by a
// dedicated test build. Production builds do not define the macro.
namespace j2b_vfs_fault
{
enum class FailureMode : uint32_t
{
    None = 0,
    BadAlloc,
    RuntimeError,
    PersistentBadAlloc,
};

void SetFailureMode(FailureMode mode) noexcept;
void ClearFailure() noexcept;
uint32_t RegistrationAttempts() noexcept;
uint32_t ActiveReaderCount() noexcept;
uint32_t OpenFileSlots() noexcept;
} // namespace j2b_vfs_fault
