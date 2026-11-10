#pragma once

// CPU-only shim for the production GameDx12J2APreparation.cpp translation
// unit. The shim is copied next to a byte-identical copy of the production
// source so that quoted engine includes do not pull in the whole xr_3da
// precompiled header.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void Msg(const char* format, ...);
