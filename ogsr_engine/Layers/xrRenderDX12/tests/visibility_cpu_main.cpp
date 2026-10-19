#include "dx12renderprep.h"
#include "dx12log.h"

// Отдельный CPU harness: без GPU/window, со всей GR-библиотекой под ASan.
int main() {
    dx12log::Open();
    const bool old=renderprep12::RunTests();
    const bool snapshot=renderprep12::RunRepresentativeTests();
    const bool visibility=renderprep12::RunVisibilityTests();
    const bool ordering=renderprep12::RunOrderingTests();
    dx12log::Close();
    return old && snapshot && visibility && ordering ? 0 : 1;
}
