#include "AuditPolicy.h"

#include <cstdio>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
}
} // namespace

int main()
{
    using namespace audit;

    // Device-removal sentinel must win over the >= expected comparison.
    Expect(ClassifyFenceCompletion(std::numeric_limits<uint64_t>::max(), 10) ==
               FenceCompletion::DeviceRemoved,
           "UINT64_MAX must be classified as DeviceRemoved");
    Expect(ClassifyFenceCompletion(std::numeric_limits<uint64_t>::max(), 1) ==
               FenceCompletion::DeviceRemoved,
           "UINT64_MAX must be DeviceRemoved for small expected values");
    Expect(ClassifyFenceCompletion(10, 10) == FenceCompletion::Completed,
           "equal fence value must be Completed");
    Expect(ClassifyFenceCompletion(0, 0) == FenceCompletion::Completed,
           "zero fence value must be Completed when expected is zero");
    Expect(ClassifyFenceCompletion(11, 10) == FenceCompletion::Completed,
           "larger fence value must be Completed");
    Expect(ClassifyFenceCompletion(9, 10) == FenceCompletion::Pending,
           "smaller fence value must be Pending");

    // Validation verdict matrix.
    {
        const ValidationStatus s =
            EvaluateValidationStatus(false, false, false, false, false, 0);
        Expect(s.verdict == ValidationVerdict::NotRequested,
               "no validation request must be NotRequested");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(true, true, true, false, false, 0);
        Expect(s.verdict == ValidationVerdict::Pass,
               "debug requested, layer+info queue enabled, zero errors must be Pass");
        Expect(s.performed, "enabled debug diagnostics must be performed");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(true, true, true, true, true, 0);
        Expect(s.verdict == ValidationVerdict::Pass,
               "GPU validation requested and enabled with info queue must be Pass");
        Expect(s.performed, "enabled GPU validation must be performed");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(true, true, true, false, false, 1);
        Expect(s.verdict == ValidationVerdict::Fail,
               "validation errors must be Fail");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(true, false, false, true, false, 0);
        Expect(s.verdict == ValidationVerdict::Inconclusive,
               "explicit --debug --gpu-validation without infrastructure must be Inconclusive");
        Expect(!s.performed, "missing infrastructure must not be performed");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(true, true, false, false, false, 0);
        Expect(s.verdict == ValidationVerdict::Inconclusive,
               "debug layer without ID3D12InfoQueue must be Inconclusive");
        Expect(!s.performed, "missing info queue must not be performed");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(true, true, true, true, false, 0);
        Expect(s.verdict == ValidationVerdict::Inconclusive,
               "GPU validation requested but disabled must be Inconclusive");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(false, false, false, true, true, 0);
        Expect(s.verdict == ValidationVerdict::Inconclusive,
               "GPU validation without debug diagnostics must be Inconclusive");
    }
    {
        const ValidationStatus s =
            EvaluateValidationStatus(false, false, false, false, false, 7);
        Expect(s.verdict == ValidationVerdict::NotRequested,
               "error count without a request must remain NotRequested");
    }

    if (g_failures != 0)
    {
        std::printf("AUDIT POLICY TESTS: FAIL (%d)\n", g_failures);
        return 1;
    }

    std::printf("FENCE SENTINEL TESTS: PASS\n");
    std::printf("VALIDATION POLICY TESTS: PASS\n");
    std::printf("AUDIT POLICY TESTS: PASS\n");
    return 0;
}
