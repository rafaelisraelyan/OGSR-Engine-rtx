# DX12/DXR Material Compatibility — Legacy First, Enhanced Optional

> **Status:** Proposed design policy, not an implemented feature or verified GPU result.
> **Applies to:** OGSR-Engine-rtx, branch `prypiat`; DX12 game-frame port (F4) and DXR material integration (G4/G5).
> **Principle:** Existing X-Ray/OGSR assets must remain usable without mandatory texture/model conversion. Modders may opt into physically based material extensions.

## 1. Why two material paths?

Existing S.T.A.L.K.E.R. mods already contain high-quality diffuse/detail/normal textures, geometry, vegetation and weather assets. A modern renderer should improve how these assets are *lit*, without turning a new asset pipeline into a prerequisite for DX12 or DXR.

The two paths share the same geometry, scene lifetime, lights and rendering infrastructure:

| Path | Required content | Expected behavior |
|---|---|---|
| **Legacy Materials** (default) | Existing supported OGSR/X-Ray assets, their original textures/flags and material parameters | Render without content repacking. Preserve recognizable artistic appearance, alpha cutouts, transparency and reasonable legacy gloss/specular behavior. Use safe calibrated defaults wherever information is missing. |
| **Enhanced Materials** (opt-in) | Explicit extra material metadata and/or maps supplied by the modder | PBR-aware shading with independently specified roughness/metallic/normal/AO/emissive when present, plus optional wetness/foliage features. Missing maps still use documented defaults. |

**Quality expectation:** Legacy content may be *less physically convincing* than well-authored Enhanced content (especially ray-traced reflections and material response), but it should **not become inherently worse than the DX11 appearance** merely because it uses the compatibility path. New shadows/lighting can benefit both paths. This is an artistic and regression target, not an automatic guarantee: DX11/DX12 shader models differ and need visual comparisons.

## 2. What DXR needs — and does not need

- **BLAS/TLAS** require compatible vertex/index geometry, transforms and lifetime management, **not** metallic/roughness textures.
- An initial **opaque RT shadow** pass can work on legacy geometry without any extra PBR maps.
- **Alpha-tested foliage, fences and perforated materials** need correct opacity/cutout handling during ray traversal (e.g. appropriate any-hit/opacity strategy). Treating every leaf card as opaque produces incorrect silhouettes and shadows.
- Shading a **ray hit outside the camera's visible G-buffer** needs access to that hit's surface attributes: material reference, interpolated UV, normals/tangent basis and any required texture data. Screen-space G-buffer values alone cannot supply arbitrary off-screen hit materials.
- **RT reflections/GI** need a consistently defined BRDF/material interpretation for plausible results. Their existence and quality are separate milestones; enabling RT shadows does **not** automatically implement reflections or GI.
- Support dynamic/skinned geometry, two-sided materials, foliage animation and GPU memory budgets as separately measured integration work; do not assume static BLAS proof covers them.

## 3. Legacy material interpretation

Use the existing OGSR material/shader classification as the source of truth. Audit actual asset formats and code paths before fixing formulas or map naming conventions.

1. Preserve diffuse/albedo, alpha cutouts, bump/normal inputs, detail textures and legacy gloss/specular cues whenever supplied; respect original UVs and texture color spaces.
2. Establish documented and **tunable** safe defaults for missing data (e.g. nonmetallic unless explicitly marked; roughness/specular chosen by material class). **Do not pretend** inferred values equal authored PBR.
3. Map legacy gloss to approximate roughness only with a visually calibrated, clamped mapping; verify the meaning and encoding of each source channel first. Converting gloss to `1 - roughness` without inspecting the source is not an accuracy guarantee.
4. Keep material-specific handling for glass, water, emissive surfaces, terrain, foliage, skin and HUD/weapon meshes; a single opaque PBR shader is insufficient.
5. Avoid double-lighting, arbitrary metallic sheen and uniform "plastic" wetness. A weather/wetness system should be parameterized and should not force all existing assets to become mirrors.
6. Preserve readable fallbacks for missing/unreadable resources and render paths without DXR. Do not mutate user assets on disk.

Implementation architecture is to be decided after source audit: a stable material handle plus an interpreted parameter/texture view should be consumable by raster and ray-hit shading. Prefer per-material overrides/sidecar metadata for Enhanced mode over global changes to legacy formats.

## 4. Enhanced material contract (proposed)

Optional inputs, subject to format/API design:
- Base color (sRGB as authored), normal (linear encoded data), roughness, metallic, AO, emissive, opacity and material-class metadata.
- Optional weather-dependent wetness response, clearcoat or foliage transmission/subsurface approximation where justified.
- Explicit mapping from a legacy material identifier to optional enhanced data; no silent asset replacement, destructive conversion or mandatory re-export of old meshes.
- Document channel packing, color-space interpretation, UV selection, fallback defaults and precedence of overrides after choosing the implementation.

A good material upgrade changes reflected energy and shading behavior coherently under sun, sky, overcast weather and rain; it does not require every modder to rebuild a complete material library.

## 5. Roadmap and acceptance

**F4 — DX12 game frame:** Prove faithful raster rendering of existing OGSR assets, including transparency/cutouts, normals/gloss, sun/sky/weather conditions and stable material lookup. Legacy path **must work before requiring Enhanced assets**.

**G1–G3 — first DXR:** Build static BLAS/TLAS, validate ray hit/miss and add a limited RT-shadow path using existing geometry. Extra PBR maps are **not an entry criterion**.

**G4 — materials/dynamics:** Validate ray-hit material retrieval for both paths, alpha-tested vegetation, animated and skinned content, dynamic resource lifetime, and correct shadow visibility through cutouts.

**G5 — optional reflections/indirect lighting:** Evaluate material fidelity and denoising with real hit attributes. Roughness/metallic authored maps should produce visible, testable differences over calibrated Legacy defaults, not imply GI is automatically available.

**Acceptance matrix:**
- Identical location, camera, time of day, weather and original asset package on DX11 vs DX12 Legacy: visual comparison, documented intentional differences, no missing geometry or severe lighting/color regressions.
- Same content in DX12 Legacy with RT shadows on/off: stable shadow quality, accurate cutout silhouettes, measured GPU and CPU milliseconds.
- DX12 Legacy vs Enhanced on a small **licensed or self-authored** material test set: direct sun, overcast, interior and rain; compare normals, roughness response, reflection quality, exposure and artistic coherence.
- Check DXR-off and non-RT fallbacks; dynamic foliage/NPCs, streaming/LOD and resize must not introduce GPU validation or lifetime faults.
- Keep representative screenshots, shader/material debug visualizations, GPU captures and performance/VRAM results. No PASS label without reproducible tests.

## 6. Content licensing

Respect third-party mod authors and licenses. Do not commit or redistribute third-party models/textures without permission; use self-authored or appropriately licensed fixtures for CI and public demonstrations.

**Decision:** Asset compatibility first. Enhanced material quality is an optional extension, not a gate that blocks F4 or initial G1–G3 DXR work.
