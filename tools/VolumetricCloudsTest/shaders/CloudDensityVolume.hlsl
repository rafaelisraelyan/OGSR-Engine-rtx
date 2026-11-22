// V2.3h-A02c Float32 density volume / Storm boundary probe.
//
// This permutation includes the same density function as the reference
// renderer. Diagnostics therefore export exactly the values the ray marcher
// integrates: base shape, shaped transfer, height envelope and final density.
// Nothing here runs the composite, exposure, tone mapping, sRGB or capture.
//
// Buffer layout, mode 0 (DensityVolume):
//   ProbeOutput[(z * resY + y) * resX + x] =
//     (rawShape, shaped, envelope, finalDensity)
//
// Buffer layout, mode 1 (StormBoundary):
//   ProbeOutput[z * resX + x] =
//     (bottom@y0, top@y0, bottom@y1, top@y1)
//
// Grid origin/cell size come from ReferenceVolume, resolution/mode from
// ReferenceGrid, boundary sample heights from ReferenceAudit.xy.
// ReferenceAudit.z carries the vertical cell size for anisotropic grids.
//
// Buffer layout, mode 2 (DensityColumns): four consecutive Y samples of one
// column are packed into one float4 record (density only). ReferenceGrid.xyz
// is (columnsX, packets, columnsZ); ReferenceAudit.xy is (yStart, yStep).

#define OGSR_PROBE 1
#define OGSR_DENSITY_MODEL 1
#include "CloudRayMarch.hlsl"

[numthreads(4, 4, 4)]
void DensityVolume(uint3 id : SV_DispatchThreadID)
{
    const uint3 resolution = max((uint3)ReferenceGrid.xyz, 1u);
    if (any(id >= resolution))
        return;

    const float cell = max(ReferenceVolume.w, 1e-3);
    const float cellY = max(ReferenceAudit.z, 1e-3);
    const float3 position =
        ReferenceVolume.xyz +
        float3((id.x + 0.5f) * cell, (id.y + 0.5f) * cellY,
               (id.z + 0.5f) * cell);
    const uint index =
        (id.z * resolution.y + id.y) * resolution.x + id.x;

#ifdef OGSR_A03B
    // A03b local-occupancy diagnostics (ReferenceGrid.w == 4): expose the
    // macro placement, the local field, their combined field and the
    // pre-detail occupancy.
    if (ReferenceLocalOccupancyEnabled() && ReferenceGrid.w > 3.5f)
    {
        float4 probeNoise;
        SampleReferenceShapeField(position, probeNoise);
        const float detail = smoothstep(
            ReferenceShapeLow(), ReferenceShapeHigh(),
            ReferenceShapeFieldValue(probeNoise));
        const float3 occPosition =
            float3(position.x, kReferenceLocalOccupancyY, position.z);
        const float macroRaw = ReferenceMacroAnomaly(occPosition);
        const float localRaw = ReferenceLocalAnomaly(occPosition);
        const float localOcc = smoothstep(
            ReferenceMacro3.z - 0.05f,
            ReferenceMacro3.z + 0.05f,
            ReferenceLocalOccupancyField(position));
        ProbeOutput[index] = float4(macroRaw, localRaw, localOcc, detail);
        return;
    }
#endif

#ifdef OGSR_A03A
    // A03a macro diagnostics (ReferenceGrid.w == 3): expose the macro field
    // stages before the final density so C2 can separate geometry loss from a
    // density drop below the diagnostic threshold.
    if (ReferenceGrid.w > 2.5f)
    {
        float4 probeNoise;
        SampleReferenceShapeField(position, probeNoise);
        const float detailRaw = ReferenceShapeFieldValue(probeNoise);
        const float detail = smoothstep(
            ReferenceShapeLow(), ReferenceShapeHigh(), detailRaw);
        const float macroRaw = ReferenceMacroField(position);
        const float mass = ReferenceMacroMass(position, detail);
        const float massCovered = ReferenceCoverageRemap(mass);
        ProbeOutput[index] = float4(macroRaw, mass, detail, massCovered);
        return;
    }
#endif

    const ReferenceDensityFields fields =
        SampleReferenceDensityFields(position);

    ProbeOutput[index] =
        float4(fields.rawShape, fields.shaped, fields.envelope,
               fields.density);
}

[numthreads(8, 8, 1)]
void StormBoundary(uint3 id : SV_DispatchThreadID)
{
    const uint3 resolution = max((uint3)ReferenceGrid.xyz, 1u);
    if (id.x >= resolution.x || id.z >= resolution.z)
        return;

    const float cell = max(ReferenceVolume.w, 1e-3);
    const float3 base =
        ReferenceVolume.xyz + (float3(id.x, 0.0, id.z) + 0.5) * cell;

    float4 result;
    if (ReferenceStorm.w > 1.5f)
    {
        // Corrected column model: identical at every sample height.
        const float2 bounds = ReferenceStormColumnBounds(base);
        result = float4(bounds.x, bounds.y, bounds.x, bounds.y);
    }
    else if (ReferenceStorm.w > 0.5f)
    {
        // Legacy S2: evaluate the same XZ at two heights to expose the
        // sample-Y dependence of the current envelope.
        const float3 p0 = float3(base.x, ReferenceAudit.x, base.z);
        const float3 p1 = float3(base.x, ReferenceAudit.y, base.z);
        const float b0 = ReferenceStormBottom(p0);
        const float t0 = max(ReferenceStormTop(p0), b0 + 150.0f);
        const float b1 = ReferenceStormBottom(p1);
        const float t1 = max(ReferenceStormTop(p1), b1 + 150.0f);
        result = float4(b0, t0, b1, t1);
    }
    else
    {
        result = float4(LayerBottom(), LayerTop(), LayerBottom(), LayerTop());
    }

    const uint index = id.z * resolution.x + id.x;
    ProbeOutput[index] = result;
}

[numthreads(8, 8, 1)]
void DensityColumns(uint3 id : SV_DispatchThreadID)
{
    const uint3 resolution = max((uint3)ReferenceGrid.xyz, 1u);
    if (id.x >= resolution.x || id.y >= resolution.y ||
        id.z >= resolution.z)
        return;

    const float cellXZ = max(ReferenceVolume.w, 1e-3);
    const float yStart = ReferenceAudit.x;
    const float yStep = max(ReferenceAudit.y, 1e-3);
    const uint packet = id.y;

    float4 densities;
    [unroll]
    for (uint k = 0u; k < 4u; ++k)
    {
        const uint sampleIndex = packet * 4u + k;
        const float3 samplePosition = float3(
            ReferenceVolume.x + (id.x + 0.5f) * cellXZ,
            yStart + (float(sampleIndex) + 0.5f) * yStep,
            ReferenceVolume.z + (id.z + 0.5f) * cellXZ);
        densities[k] =
            SampleReferenceDensityFields(samplePosition).density;
    }

    const uint index = (id.z * resolution.y + id.y) * resolution.x + id.x;
    ProbeOutput[index] = densities;
}
