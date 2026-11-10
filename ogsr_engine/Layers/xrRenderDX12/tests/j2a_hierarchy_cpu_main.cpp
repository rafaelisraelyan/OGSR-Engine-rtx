#include "dx12levelhierarchy.h"

#include <cstdio>

int main()
{
    const bool ok = levelhierarchy12::RunTests();
    std::printf("J2A_HIERARCHY_RESULT %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
