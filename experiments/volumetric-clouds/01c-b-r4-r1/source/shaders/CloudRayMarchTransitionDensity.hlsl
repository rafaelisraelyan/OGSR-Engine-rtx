// V-WEATHER-01B Gate B1: parameterized copies of the existing density models.
// The original static paths in CloudRayMarch.hlsl remain untouched; these
// copies are used only by the opt-in transition mode.
//
// CloudDensityState is declared in CloudRayMarch.hlsl. It carries the fields
// that differ between the overcast (legacy) and rain_sf (storm-front) states.
// No new noise, density formula or morphology is introduced.

float LayerThicknessState(CloudDensityState state)
{
    return max(state.LayerShape.x - state.Morphology.w, 1.0);
}

float3 WeatherWorldPositionState(float3 position, CloudDensityState state)
{
    return position - float3(state.WindNoiseScale.x, 0.0,
                             state.WindNoiseScale.z);
}

float4 SampleWeatherState(float3 position, CloudDensityState state)
{
    if ((uint)state.WeatherC.w == (uint)1)
        return float4(0.5, 0.5, 0.5, 0.5);
    const float2 uv = WeatherWorldPositionState(position, state).xz *
                      state.WeatherB.x;
    return WeatherTexture.SampleLevel(NoiseSampler, uv, 0);
}

float LocalHeightFractionState(float3 position, float4 weather,
                               float overcastWeight, float4 baseNoise,
                               CloudDensityState state)
{
    const uint ablation = (uint)state.WeatherC.w;
    const bool noHeightVariation = ablation == 15u;
    const float variation = noHeightVariation ? 0.0 : state.WeatherC.x;
    const float bottom = state.Morphology.w +
        (weather.b - 0.5) * variation * (0.35 + 0.65 * overcastWeight);
    const float top = state.LayerShape.x +
        (weather.g - 0.5) * variation * 0.70;
    float height = (position.y - bottom) / max(top - bottom, 1.0);
    const float warp = (baseNoise.r - 0.5) * 0.28 +
                       (baseNoise.b - 0.5) * 0.12 +
                       (noHeightVariation ? 0.0 : (weather.a - 0.5) * 0.08);
    height += warp * (0.55 + 0.45 * overcastWeight);
    return height;
}

float FrontMaskState(float3 position, float4 baseNoise,
                     CloudDensityState state)
{
    const float2 direction = normalize(float2(0.92, 0.39));
    float offset = state.WeatherB.y;
    float width = max(state.WeatherB.z, 1.0);
    if (state.VisualFlags.y > 0.5)
    {
        const float height = saturate(
            (position.y - state.Morphology.w) /
            max(LayerThicknessState(state), 1.0));
        const float width0 = max(state.WeatherB.z, 1.0);
        offset += (height - 0.5) * width0 * 2.60;
        offset += (baseNoise.r - 0.5) * width0 * 1.30;
        offset += (baseNoise.g - 0.5) * height * width0 * 0.90;
        width = width0 * (0.55 + 0.90 * baseNoise.b);
    }
    const float distance = dot(
        WeatherWorldPositionState(position, state).xz, direction) - offset;
    return 1.0 - smoothstep(-width, width, distance);
}

StormFrontSample SampleStormFrontState(float3 position, CloudDensityState state)
{
    StormFrontSample result;
    result.field = 0.0;
    result.band = 0.0;
    result.massGate = 0.0;
    result.vertical = 0.0;

    const float2 frontDir = normalize(float2(0.92, 0.39));
    const float2 tangent = float2(-frontDir.y, frontDir.x);
    const float2 xz = position.xz - state.WindNoiseScale.xz;
    const float along = dot(xz, tangent);
    const float across = dot(xz, frontDir);

    const float bend =
        state.StormFrontParams0.x * sin(along / state.StormFrontParams0.z + 1.7) +
        state.StormFrontParams0.y * sin(along / state.StormFrontParams0.w + 4.1);
    const float depth =
        state.StormFrontParams1.x +
        state.StormFrontParams1.y * (0.5 + 0.5 * sin(
            along / state.StormFrontParams1.z + 2.3));
    const float signedDistance = across - (state.WeatherB.y + bend);

    // Cheap reject before the extra noise fetch. The smooth band is inside
    // this conservative interval; the noise warp is accounted for.
    if (signedDistance < -depth * 1.5 - 500.0 ||
        signedDistance > depth * 1.5 + 500.0)
        return result;
    if (position.y < state.Morphology.w - 600.0 ||
        position.y > state.StormFrontParams2.z)
        return result;

    const float3 q = float3(
        along / 12000.0,
        position.y / 6000.0,
        across / 9000.0);
    const float4 n0 = NoiseTexture.SampleLevel(
        NoiseSampler, q + float3(0.37, 0.11, 0.53), 0);
    const float weather = SampleWeatherState(position, state).r;

    const float mass = saturate(
        n0.r * 0.55 + n0.b * 0.30 + n0.g * 0.15 +
        (weather - 0.5) * 0.10);
    const float massGate = smoothstep(
        state.StormFrontParams2.x, state.StormFrontParams2.y, mass);

    const float baseY =
        state.Morphology.w + 200.0 + (n0.g - 0.5) * state.StormFrontParams3.x;
    float topY =
        state.LayerShape.x + (n0.b - 0.5) * state.StormFrontParams3.y +
        mass * state.StormFrontParams3.z;
    topY = min(topY, state.StormFrontParams2.z - 300.0);

    const float vertical =
        smoothstep(baseY, baseY + 450.0, position.y) *
        (1.0 - smoothstep(topY - 650.0, topY, position.y));

    const float warpedS = signedDistance +
        (n0.r - 0.5) * 500.0 + (n0.g - 0.5) * 250.0;
    const float leading = smoothstep(
        -depth * 0.70, -depth * 0.10, warpedS);
    const float trailing = 1.0 - smoothstep(
        depth * 0.35, depth * 1.20, warpedS);
    const float band = leading * trailing;

    float field = band * massGate * vertical;
    field *= lerp(0.70, 1.0, smoothstep(0.20, 0.80, n0.g));

    result.field = saturate(field);
    result.band = band;
    result.massGate = massGate;
    result.vertical = vertical;
    return result;
}

float SampleStormFrontDensityState(float3 position, out CloudDensityDebug dbg, CloudDensityState state)
{
    dbg.height = 0.0;
    dbg.profile = 0.0;
    dbg.shaped = 0.0;
    dbg.macroR = 0.0;
    dbg.midG = 0.0;
    dbg.weatherR = 0.0;
    dbg.typeValue = 0.0;
    dbg.coverage = 0.0;

    const uint renderDebug = (uint)state.Shaping.w;
    const bool rawIntegralMode =
        renderDebug == 21u || renderDebug == 22u || renderDebug == 23u;
    const StormFrontSample sample = SampleStormFrontState(position, state);
    const uint fieldDebug = (uint)state.StormFrontParams3.w;

    dbg.height = saturate(
        (position.y - state.Morphology.w) / max(state.LayerShape.x - state.Morphology.w, 1.0));
    dbg.coverage = sample.field;
    dbg.weatherR = SampleWeatherState(position, state).r;

    if (!rawIntegralMode)
    {
        if (fieldDebug == 1u)
            return sample.field;
        if (fieldDebug == 2u)
            return sample.massGate;
        if (fieldDebug == 3u)
            return sample.vertical;
    }

    const float densityMultiplier = max(state.LayerShape.z, 0.0);
    return saturate(
        sample.field * state.StormFrontParams1.w * densityMultiplier);
}

float SampleLegacyDensityState(float3 position, out CloudDensityDebug dbg, CloudDensityState state)
{
    dbg.height = 0.0;
    dbg.profile = 0.0;
    dbg.shaped = 0.0;
    dbg.macroR = 0.0;
    dbg.midG = 0.0;
    dbg.weatherR = 0.0;
    dbg.typeValue = 0.0;
    dbg.coverage = 0.0;

    const float coverageGlobal = saturate(state.LayerShape.y);
    const float densityMultiplier = max(state.LayerShape.z, 0.0);
    if (coverageGlobal <= 0.0 || densityMultiplier <= 0.0)
        return 0.0;

    const float scale = state.WindNoiseScale.w;
    const float3 windOffset = state.WindNoiseScale.xyz;
    const float4 weather = SampleWeatherState(position, state);
    const uint ablation = (uint)state.WeatherC.w;
    const bool macroOnly = ablation == 9u;
    const bool noWarp = ablation == 8u;
    const bool base3d = ablation == 13u;
    const bool noBaseWarp = ablation == 14u;
    const bool noHeightVariation = ablation == 15u;
    const bool noAnisotropy = ablation == 16u;
    const bool flatBottom = ablation == 17u;
    const bool noTypeProfile = ablation == 18u;
    const bool noTypeThresholds = ablation == 19u;
    const bool noTypeAnisotropy = ablation == 20u;
    const bool noTypeLocalBounds = ablation == 21u;
    const bool noTypeShape = ablation == 22u;
    const float typeValue = saturate(
        weather.g * 0.90 + state.WeatherA.y + (weather.b - 0.5) * 0.08);
    const float3 typeWeights = ablation == 7u
        ? float3(1.0, 0.0, 0.0)
        : CloudTypeWeights(typeValue);
    // V2.3c factor isolation: each weight set changes exactly one mechanism.
    const float3 localWeights =
        noTypeLocalBounds ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float3 profileWeights =
        noTypeProfile ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float3 anisotropyWeights =
        noTypeAnisotropy ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float3 shapeWeights =
        noTypeShape ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float overcastWeight = localWeights.z;

    // Diagnostic base-3d: the base field is normally a 2D XZ field (z=0.45).
    // This variant adds a slow vertical component to test whether the fixed
    // third coordinate is responsible for vertically coherent pillars.
    const float rawHeight = saturate(
        (position.y - state.Morphology.w) / max(state.LayerShape.x - state.Morphology.w, 1.0));
    const float3 baseCoordinate = float3(
        (position.x - windOffset.x) * scale * 0.70,
        (position.z - windOffset.z) * scale * 0.70,
        base3d ? (0.45 + rawHeight * state.Shaping.x * 0.35) : 0.45);
    const float4 baseNoise = (!macroOnly)
        ? (ablation == 3u ? float4(0.5, 0.5, 0.5, 1.0)
                          : NoiseTexture.SampleLevel(NoiseSampler,
                                                     baseCoordinate, 0))
        : float4(0.5, 0.5, 0.5, 1.0);
    // Diagnostic no-base-warp: keep baseNoise for the domain warp, but remove
    // its contribution to the local height field.
    const float4 warpNoise = noBaseWarp
        ? float4(0.5, 0.5, 0.5, 1.0) : baseNoise;

    const float height = LocalHeightFractionState(position, weather, overcastWeight, warpNoise, state);
    dbg.height = height;
    const float profile = CloudHeightProfile(height, profileWeights);
#if OGSR_FORMULA != 2
    if (profile <= 0.0)
        return 0.0;
#endif
    dbg.profile = profile;

    const float storm = saturate(state.WeatherA.w * (0.55 + 0.45 * weather.a));
    const float front = FrontMaskState(position, baseNoise, state) * state.WeatherB.w;
    const float frontBoost = front * (0.25 + 0.35 * storm);

    // coverageGlobal is a control, not a visibility multiplier: it changes the
    // threshold/contrast of the macro field, while the weather map supplies the
    // spatial organization.
    const float coverage = saturate(
        0.15 + coverageGlobal * 0.65 + state.WeatherA.x +
        (weather.r - 0.5) * 0.70 + frontBoost);
    const float weatherGate = smoothstep(
        0.10, 0.55, weather.r + state.WeatherA.x * 0.35);
    const float gatedCoverage = coverage * weatherGate;
    dbg.weatherR = weather.r;
    dbg.typeValue = typeValue;
    dbg.coverage = coverage;

    // Type-specific vertical anisotropy: cumulus gets a full 3D tower, while
    // stratocumulus and overcast are progressively flatter layers. This is a
    // shape control, not a lighting trick.
    const float verticalAnisotropy = noAnisotropy ? 1.0 :
        (anisotropyWeights.x * 1.00 + anisotropyWeights.y * 0.55 +
         anisotropyWeights.z * 0.55);
#if OGSR_FORMULA == 4
    // F4: the macro 3D morphology is sampled directly in world XYZ. The
    // normalized height still drives the cloud height profile.
    const float3 coordinate = float3(
        (position.x - windOffset.x) * scale,
        position.y * scale * 2.2f * verticalAnisotropy,
        (position.z - windOffset.z) * scale);
#else
    const float3 coordinate = float3(
        (position.x - windOffset.x) * scale,
        position.y * scale * 0.75f,
        (position.z - windOffset.z) * scale);
#endif
    // Gentle low-frequency domain warp. The previous 0.55-noise-unit warp
    // created high-frequency interference/chevrons; 0.12 is enough to break
    // lattice alignment without destroying the mass silhouette.
    const float3 warpedCoordinate = noWarp ? coordinate : float3(
        coordinate.x + (baseNoise.r - 0.5) * 0.12,
        coordinate.y + (baseNoise.g - 0.5) * 0.12,
        coordinate.z + (baseNoise.b - 0.5) * 0.10);

    const float4 macro = NoiseTexture.SampleLevel(
        NoiseSampler, warpedCoordinate * 0.42 + float3(0.17, 0.31, 0.07), 0);
    dbg.macroR = macro.r;
    const float4 mid = (ablation == 2u || macroOnly)
        ? float4(0.5, 0.5, 0.5, 1.0)
        : NoiseTexture.SampleLevel(
              NoiseSampler, warpedCoordinate * 0.55 + float3(0.11, 0.47, 0.29),
              0);
    dbg.midG = mid.g;
    const float4 fine = (ablation == 12u)
        ? NoiseTexture.SampleLevel(
              NoiseSampler, warpedCoordinate * 3.1 + float3(0.53, 0.11, 0.37),
              0)
        : float4(0.5, 0.5, 0.5, 1.0);

    const float formulaThresholdBias =
        (OGSR_FORMULA == 1 || OGSR_FORMULA == 3)
            ? clamp(LightingFlags.z, -0.20f, 0.20f)
            : 0.0f;
#if OGSR_FORMULA == 3
    // F3: coverage is separated from the shape threshold. The type shape uses
    // fixed thresholds; spatial placement is applied once after the shape.
    const float thresholdCumulus = 0.55f + formulaThresholdBias;
    const float thresholdStrat = 0.45f + formulaThresholdBias;
    const float thresholdOvercast = 0.38f + formulaThresholdBias;
    const float weatherPlacement = smoothstep(
        0.10f, 0.55f, weather.r + state.WeatherA.x * 0.35f);
    const float coverageScale = smoothstep(
        0.15f, 0.55f, coverageGlobal);
    const float placement = weatherPlacement * coverageScale;
#else
    const float thresholdCumulus =
        lerp(0.66, 0.44, gatedCoverage) + formulaThresholdBias;
    const float thresholdStrat = noTypeThresholds
        ? thresholdCumulus : lerp(0.58, 0.34, gatedCoverage) +
            formulaThresholdBias;
    const float thresholdOvercast = noTypeThresholds
        ? thresholdCumulus : lerp(0.52, 0.30, gatedCoverage) +
            formulaThresholdBias;
    const float placement = 1.0f;
#endif

    // Macro shape owns the mass; low-frequency Worley gives it rounded cells.
    // Detail/fine only erode the boundary, not the dense core.
#if OGSR_FORMULA == 1
    // F1: Perlin-Worley (.b) is the primary shape field. The existing
    // mid-frequency Worley adds a small amount of cell organization.
    const float f1Base = macro.b * 0.85f + mid.g * 0.15f;
    const float cumulusBase = f1Base;
    const float stratBase = f1Base;
    const float overcastBase = f1Base;
#else
    const float cumulusBase = macro.r * 0.70 + macro.b * 0.20 + mid.g * 0.10;
    const float stratBase = macro.r * 0.70 + macro.b * 0.15 + mid.g * 0.15;
    const float overcastBase = macro.r * 0.70 + macro.b * 0.20 + mid.g * 0.10;
#endif

    const float cumulusShape = saturate(
        (cumulusBase - thresholdCumulus) / max(1.0 - thresholdCumulus, 0.05));
    const float stratShape = saturate(
        (stratBase - thresholdStrat) / max(1.0 - thresholdStrat, 0.05));
    const float overcastShape = saturate(
        (overcastBase - thresholdOvercast) /
        max(1.0 - thresholdOvercast, 0.05));

    float shaped = noTypeShape
        ? cumulusShape
        : (cumulusShape * shapeWeights.x +
           stratShape * shapeWeights.y +
           overcastShape * shapeWeights.z);
    shaped = shaped * shaped * (3.0 - 2.0 * shaped);
    shaped = pow(saturate(shaped), 1.10);
    dbg.shaped = shaped;

#if OGSR_FORMULA == 2
    const float cumulusProfileF2 = CloudHeightProfile(height, float3(1.0, 0.0, 0.0));
    const float stratProfileF2 = CloudHeightProfile(height, float3(0.0, 1.0, 0.0));
    const float overcastProfileF2 = CloudHeightProfile(height, float3(0.0, 0.0, 1.0));
    const float cumulusDensityF2 = ShapeTransferF2(cumulusShape) * cumulusProfileF2;
    const float stratDensityF2 = ShapeTransferF2(stratShape) * stratProfileF2;
    const float overcastDensityF2 = ShapeTransferF2(overcastShape) * overcastProfileF2;
    float shapeForEdge = typeWeights.x * ShapeTransferF2(cumulusShape) +
        typeWeights.y * ShapeTransferF2(stratShape) +
        typeWeights.z * ShapeTransferF2(overcastShape);
    float densityBase = typeWeights.x * cumulusDensityF2 +
        typeWeights.y * stratDensityF2 +
        typeWeights.z * overcastDensityF2;
    dbg.shaped = shapeForEdge;
#else
    float shapeForEdge = shaped;
    float densityBase = shaped * profile;
#endif
    float formulaDifference = 0.0f;
#if OGSR_FORMULA == 2
    formulaDifference = abs(densityBase - shaped * profile);
#endif
    float prototypeWeight = 0.0f;

#if OGSR_SHAPE_PROTOTYPE == 1
    // Prototype A: deep convective masses built from smoothed Worley/Perlin
    // cell distance fields, soft-unioned. The macro Perlin threshold is no
    // longer the primary shape.
    {
        const float billowA = 1.0f - abs(2.0f * macro.r - 1.0f);
        const float billowB = 1.0f - abs(2.0f * mid.r - 1.0f);
        const float blobA = smoothstep(0.55f, 0.88f, billowA);
        const float blobB = smoothstep(0.60f, 0.92f, billowB);
        const float cells = 1.0f - (1.0f - blobA) * (1.0f - blobB * 0.50f);
        const float topStart = 0.65f + 0.10f * storm;
        const float convectiveProfile =
            smoothstep(0.06f, 0.25f, height) *
            (1.0f - smoothstep(topStart, 0.92f, height));
        const float placement = smoothstep(0.35f, 0.80f, gatedCoverage);
        const float densityA = cells * convectiveProfile * placement;
        const float convectiveWeight =
            saturate(typeWeights.x + storm * 0.55f);
        prototypeWeight = convectiveWeight;
        densityBase = lerp(densityBase, densityA, convectiveWeight);
        shapeForEdge = lerp(shapeForEdge, cells * placement,
                            convectiveWeight);
    }
#elif OGSR_SHAPE_PROTOTYPE == 2
    // Prototype B: a continuous layered deck with 3D thickness and limited
    // large-scale boundary erosion. No thresholded macro iso-surface.
    {
        const float deckBottom = clamp(
            0.05f + (mid.r - 0.5f) * 0.25f + (macro.b - 0.5f) * 0.08f,
            0.0f, 0.40f);
        const float deckTop = clamp(
            0.86f + (macro.b - 0.5f) * 0.15f, 0.60f, 1.0f);
        const float soft = 0.10f + 0.06f * (1.0f - mid.g);
        float deck = smoothstep(deckBottom, deckBottom + soft, height) *
            (1.0f - smoothstep(deckTop - soft, deckTop, height));
        deck *= lerp(0.45f, 1.0f, macro.b) *
            lerp(0.65f, 1.0f, mid.g);
        const float erosion = smoothstep(0.20f, 0.75f, mid.r);
        const float boundaryBand =
            smoothstep(0.0f, 0.25f, height) *
            (1.0f - smoothstep(0.65f, 1.0f, height));
        deck *= lerp(1.0f, erosion, boundaryBand);
        const float gaps = smoothstep(
            0.06f, 0.38f, weather.r + (macro.b - 0.5f) * 0.25f);
        deck *= lerp(1.0f, gaps, typeWeights.y);
        const float densityB = deck;
        const float layeredWeight =
            saturate(typeWeights.y + typeWeights.z);
        prototypeWeight = layeredWeight;
        densityBase = lerp(densityBase, densityB, layeredWeight);
        shapeForEdge = lerp(shapeForEdge, deck, layeredWeight);
    }
#endif

    const float edgeFactor = macroOnly ? 0.0 :
        1.0 - smoothstep(0.35, 0.85, shapeForEdge);
    // Detail/fine are intentionally not multiplied into the V2.2b cumulus
    // density: their 128Ві slice signature produced regular stripes. They stay
    // in the shader behind diagnostic-only paths for future cirrus research.
    // Boundary erosion now comes from the low-frequency Worley mass field.
    float density = densityBase;
    density *= lerp(1.0, lerp(0.72, 1.0, mid.g), edgeFactor * 0.55);
#if OGSR_FORMULA == 5
    // F5: controlled medium-frequency boundary erosion using the existing
    // macro Worley channel. No new texture fetch and no high-frequency path.
    const float erosionStrength = clamp(LightingFlags.z, 0.0f, 1.0f);
    const float controlledErosion = smoothstep(0.45f, 0.90f, macro.g);
    density *= lerp(1.0f, controlledErosion,
                    edgeFactor * erosionStrength);
#endif

#if OGSR_FORMULA == 3
    density *= placement;
#else
    density *= weatherGate;
    const float verticalErosion = smoothstep(
        0.25, 0.85, mid.g + (macro.b - 0.5) * 0.25);
    density *= lerp(0.55, 1.0, verticalErosion);
    if (state.VisualFlags.x > 0.5)
    {
        const float4 breakup = NoiseTexture.SampleLevel(
            NoiseSampler, warpedCoordinate * 1.9 + float3(0.37, 0.11, 0.53), 0);
        density *= lerp(0.55, 1.0,
                        smoothstep(0.20, 0.80,
                                   breakup.g + (breakup.r - 0.5) * 0.20));
    }
#endif
    density *= lerp(0.65, 1.35, weather.b);
    density *= 1.0 + storm * 0.35 + front * 0.25;
    // The top surface is a 3D height field, not a fixed plane. This is the
    // difference between a rippled ceiling and billowing cloud tops.
    float topThreshold = 0.88f -
        shapeWeights.x * 0.10f - shapeWeights.y * 0.04f +
        (macro.r - 0.5f) * 0.20f + (mid.g - 0.5f) * 0.10f;
    // V2.3d candidate 5: cumulus top widening is applied through the
    // threshold above, not by moving the top boundary.
    // V2.3c diagnostic: top fade width. A longer fade below the threshold
    // turns the thin cumulus spire into a softer, wider cloud top.
    const float topFade = clamp(state.Morphology.z, 0.02f, 0.30f);
    const float topCut = 1.0f - smoothstep(topThreshold - topFade,
                                           topThreshold + 0.06f, height);
    density *= lerp(topCut, 1.0f, prototypeWeight);
    // Ragged lower edge: the overcast base must not look like a flat ceiling.
    if (!flatBottom)
    {
        const float bottomCut = smoothstep(
            0.0f, 0.12f, height + (macro.r - 0.5f) * 0.08f);
        density *= lerp(bottomCut, 1.0f, prototypeWeight);
    }
    density *= densityMultiplier;
#if OGSR_FORMULA == 2
    if ((uint)state.Shaping.w == 17u)
        return saturate(formulaDifference * 4.0f);
#endif
    return saturate(density);
}
