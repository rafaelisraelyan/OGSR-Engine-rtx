#pragma once

#include <cstdlib>
#include <cstring>

// F4.0-J2B-M23-R1: test-only fault selection.
//
// Runtime environment fault injection is compiled only into a dedicated
// probe build (OGSR_M23_FAULT_INJECTION). The normal feature-on binary has no
// environment-driven fault path, addressing the review note about
// OGSR_DX12_GAME_M23_FORCE_UNKNOWN living in the experimental production
// binary.
namespace m23fault12
{
#ifdef OGSR_M23_FAULT_INJECTION

inline bool Is(const char* name) noexcept
{
    if (name == nullptr)
        return false;
    const char* value = std::getenv("OGSR_DX12_GAME_M23_FAULT");
    if (value == nullptr || std::strcmp(value, name) != 0)
        return false;
    const char* once = std::getenv("OGSR_DX12_GAME_M23_FAULT_ONCE");
    if (once != nullptr && once[0] != '\0')
    {
        static bool consumed = false;
        if (consumed)
            return false;
        consumed = true;
    }
    return true;
}

inline bool ErrorOom() noexcept
{
    const char* value = std::getenv("OGSR_DX12_GAME_M23_ERROR_OOM");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

#else

inline bool Is(const char*) noexcept { return false; }
inline bool ErrorOom() noexcept { return false; }

#endif
} // namespace m23fault12
