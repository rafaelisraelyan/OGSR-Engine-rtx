#include <cstddef>

// Test-only v1-shaped DLL. It deliberately exports no v2 API, so the loader
// must stop at MissingApiInfo without attempting any v2 call.
extern "C" __declspec(dllexport) int OgsrPhysXRagdollStep(void* handle, float deltaSeconds, char* error,
                                                         std::size_t errorSize)
{
    (void)handle;
    (void)deltaSeconds;
    (void)error;
    (void)errorSize;
    return 0;
}
