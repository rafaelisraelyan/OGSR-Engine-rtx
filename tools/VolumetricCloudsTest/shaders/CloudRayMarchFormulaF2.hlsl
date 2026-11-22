// F2: per-type density blending. Transfer is applied to each type shape
// before the type weights, then the full densities (shape * profile) are mixed.
#define OGSR_FORMULA 2
#include "CloudRayMarch.hlsl"
