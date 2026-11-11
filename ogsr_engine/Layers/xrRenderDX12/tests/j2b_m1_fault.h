#pragma once

#include <cstdint>

// Test-only fault hook. This header is included only when
// OGSR_J2B_CPU_FAULT_INJECTION is defined by a test build; production builds
// do not define it and never link these symbols.
namespace j2b_m1_fault
{
enum class Point
{
    AfterRead,
    BeforePublish,
};

void MaybeThrow(Point point);
bool SkipSubmit(uint32_t index);
} // namespace j2b_m1_fault
