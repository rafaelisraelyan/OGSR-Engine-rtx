// V2.3h reference density probe permutation. Probe and render use the same
// CloudReferenceDensity.hlsl path and the same stage in LightingFlags.w.
#define OGSR_PROBE 1
#define OGSR_DENSITY_MODEL 1
#include "CloudRayMarch.hlsl"
