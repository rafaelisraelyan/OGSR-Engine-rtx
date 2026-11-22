// Probe-only permutation of the cloud ray marcher. The default permutation
// (CloudRayMarch.hlsl) does not declare the probe UAV or the ray-profile
// branch, so it keeps the V2.3c register/performance profile.
#define OGSR_PROBE 1
#include "CloudRayMarch.hlsl"
