#include "dx12renderprep.h"
#include "dx12packetplan.h"
#include "dx12fencewait.h"
#include "dx12transform_tests.h"
#include "dx12geometry_tests.h"
#include "dx12xrayasset_tests.h"
#include "dx12log.h"

// Отдельный CPU harness: без GPU/window, со всей GR-библиотекой под ASan.
int main() {
    dx12log::Open();
    const bool old=renderprep12::RunTests();
    const bool snapshot=renderprep12::RunRepresentativeTests();
    const bool visibility=renderprep12::RunVisibilityTests();
    const bool ordering=renderprep12::RunOrderingTests();
    const bool packetPlan=renderprep12::RunPacketBatchPlanTests();
    const bool fenceWait=dx12fence12::RunFenceWaitTests();
    const bool transform=smoketransform12::RunTransformTests();
    const bool geometry=smokegeometry12::RunGeometryTests();
    const bool xray=smokexray12::RunXrayAssetTests(nullptr, nullptr);
    dx12log::Close();
    return old && snapshot && visibility && ordering && packetPlan && fenceWait && transform && geometry && xray ? 0 : 1;
}
