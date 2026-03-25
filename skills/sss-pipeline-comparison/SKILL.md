---
name: sss-pipeline-comparison
description: Side-by-side comparison of the Filament Burley SSS pipeline against the Unreal Engine Burley/Subsurface Profile pipeline. Produces a structured table mapping each pipeline stage, parameter, and feature between the two engines.
---

# SSS Pipeline Comparison (Filament vs Unreal Engine)

Use this skill to produce a side-by-side, stage-by-stage comparison of the screen-space Burley Normalized Diffusion SSS pipelines in Filament and Unreal Engine.

## When to Use

- The user asks to compare Filament SSS against Unreal SSS
- The user wants to understand architectural differences between the two engines' SSS paths
- The user needs to identify what Filament is missing relative to Unreal, or what it does differently
- The user wants a structured reference document for porting decisions

## Inputs

- **Filament repo root:** current workspace (`/Users/aspirin2ds/Workspace/github/filament`)
- **Unreal Engine source:** `../UnrealEngine`

## Source Files to Read

Before producing the comparison, read these files to anchor in current source truth.

### Filament

| Component | File |
|---|---|
| Shading model shader | `shaders/src/surface_shading_model_subsurface_burley.fs` |
| Dual specular BRDF | `shaders/src/surface_brdf.fs` (search `burleyDualSpecularLobe`) |
| MRT output | `shaders/src/surface_main.fs` (SUBSURFACE_BURLEY block) |
| Material inputs | `shaders/src/surface_material_inputs.fs` |
| Indirect lighting SSS | `shaders/src/surface_light_indirect.fs` (search `g_sssDiffuse`) |
| Blur material shader | `filament/src/materials/sss/sssBlur.mat` |
| Post-process integration | `filament/src/PostProcessManager.cpp` (search `subsurfaceScatteringBlur`) |
| Buffer allocation | `filament/src/RendererUtils.cpp` (search `sssDiffuse`) |
| Renderer scheduling | `filament/src/details/Renderer.cpp` (search `subsurfaceScatteringBlur`) |
| View options API | `filament/include/filament/Options.h` (search `SubsurfaceScatteringOptions`) |
| Sample app | `samples/sample_sss_burley.cpp` |

### Unreal Engine

| Component | File |
|---|---|
| SSS profile struct | `../UnrealEngine/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h` |
| Burley common math | `../UnrealEngine/Engine/Shaders/Private/BurleyNormalizedSSSCommon.ush` |
| Burley sampling shader | `../UnrealEngine/Engine/Shaders/Private/SubsurfaceBurleyNormalized.ush` |
| Separable SSS kernel | `../UnrealEngine/Engine/Shaders/Private/SeparableSSS.ush` |
| Post-process SSS shader | `../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf` |
| Post-process SSS C++ | `../UnrealEngine/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp` |
| SSS profile common | `../UnrealEngine/Engine/Shaders/Private/SubsurfaceProfileCommon.ush` |
| Transmission common | `../UnrealEngine/Engine/Shaders/Private/TransmissionCommon.ush` |
| CPU-side FalloffColor mapping | `../UnrealEngine/sss/BurleyNormalizedSSS.cpp` |
| CPU-side profile setup | `../UnrealEngine/sss/SubsurfaceProfile.cpp` |

## Comparison Structure

Produce the comparison using the following stage-by-stage format. For each stage, show what each engine does, where the code lives, and note key differences.

---

### Stage 1: Profile / Parameter Definition

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Profile system** | View-level `SubsurfaceScatteringOptions` struct + per-material `subsurfaceColor` + `scatteringDistance` + `radiusScale` written to 4 MRT buffers | `USubsurfaceProfile` asset with `FSubsurfaceProfileStruct`; up to 256 profiles stored in a texture atlas (`FSubsurfaceProfileTexture`) | Gap |
| **FalloffColor → SurfaceAlbedo + DMFP** | CPU-side (`PostProcessManager.cpp`): `falloffColor` mapped to `sssAlbedo` and `dmfpRaw` via UE's `MapFallOffColor2SurfaceAlbedoAndDiffuseMeanFreePath` polynomial. `sssAlbedo = 0.906*fc + 0.00004`, DMFP via degree-5 polynomial. DMFP factored into `dmfpScale` (max channel) and `dmfpRatios`. | CPU-side (`BurleyNormalizedSSS.cpp`): identical `MapFallOffColor2SurfaceAlbedoAndDiffuseMeanFreePath` polynomial mapping. `SurfaceAlbedo.a` packed as albedo of dominant DMFP channel. | **Aligned** |
| **Scattering distance** | `falloffColor` → `dmfpRaw` (degree-5 polynomial) → `dmfpScale * dmfpRatios`. `options.scatteringDistance * dmfpScale` passed as global scale; `dmfpRatios` folded into `subsurfaceColor`. Per-pixel `scatteringDistance` stored in MRT params `.a`. | Per-profile `MeanFreePathColor` (RGB) + `MeanFreePathDistance` (float, cm); combined into per-channel DMFP stored in profile texture | **Aligned** (math matches; Filament uses view-global + per-pixel MRT rather than per-profile atlas) |
| **World unit scale** | View-global `worldUnitScale` (default 1.0) | Per-profile `WorldUnitScale` (default 0.1, in cm) | Aligned |
| **Tint** | View-global `subsurfaceColor` (float3); per-material `subsurfaceColor` written to `sssParams.rgb`. DMFP ratios folded in: `subsurfaceColor * dmfpRatios`. | Per-profile `Tint` (RGBA); controls lerp between original and scattered | Aligned |
| **Dual specular** | Per-material: `roughness0` (0.75), `roughness1` (1.3), `lobeMix` (0.85) | Per-profile: `Roughness0` (0.75), `Roughness1` (1.3), `LobeMix` (0.85) | Aligned |
| **Temporal noise** | `temporalNoise` bool in `SubsurfaceScatteringOptions`; toggles R2 quasi-random per-pixel per-frame jitter vs deterministic Fibonacci spiral | Always-on per-pixel R2 sequence with frame-seeded rotation + exponential moving average across frames | **Partial** (Filament has R2 jitter but no explicit temporal accumulation — relies on downstream TAA) |
| **Fast sample normals** | `fastSampleNormals` bool: toggles 1-tap vs 5-tap normal reads for bilateral weight | N/A (UE reads normals from GBuffer, single tap) | Filament-specific optimization |
| **Profile ID** | No profile ID system; all SSS pixels share blur parameters from view + per-pixel MRT values | GBuffer encodes profile ID (0-255); blur reads per-profile kernel weights from profile texture | Gap |
| **Source files** | `Options.h`, `PostProcessManager.cpp`, `surface_material_inputs.fs`, `surface_main.fs` | `SubsurfaceProfile.h`, `SubsurfaceProfileCommon.ush`, `BurleyNormalizedSSS.cpp` |  |

### Stage 2: Setup / Decomposition (Color Pass)

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Diffuse/specular separation** | Explicit MRT: `g_sssDiffuse` accumulates only diffuse lighting (specular excluded); written to `fragColor1.rgb` | Checkerboard or full-res: `ReconstructLighting()` separates diffuse and specular from composited scene color using GBuffer data | Aligned |
| **Setup buffer contents** | MRT 1 (`sssDiffuse`): diffuse RGB + membership mask (A). MRT 2 (`sssNormal`): view-normal XYZ + thickness (A). MRT 3 (`sssParams`): subsurfaceColor RGB + scatteringDistance (A). MRT 4 (`sssAlbedo`): surface albedo RGB + radiusScale (A). All RGBA16F. | No separate setup buffer; blur reads scene color directly; GBuffer provides normals, profile ID, thickness, base color | Different approach, equivalent result |
| **SSS membership** | `g_sssMask` = max channel of `subsurfaceColor`; written to `fragColor1.a` | GBuffer shading model ID checked (`UseSubsurfaceProfile()`); stencil bit marks SSS pixels for tile classification | Aligned |
| **Normal storage** | Dedicated MRT: shaded view-space normal (matches material normal map) + thickness | GBuffer world normal; no separate SSS normal buffer | Aligned |
| **Tile classification** | None; blur applied to all pixels, early-out on mask/depth | Indirect dispatch: setup pass classifies tiles as Burley or Separable into dispatch buffers; only SSS tiles are processed | Gap |
| **Source files** | `surface_shading_model_subsurface_burley.fs`, `surface_main.fs`, `RendererUtils.cpp` | `PostProcessSubsurface.usf` (setup pass), `SubsurfaceBurleyNormalized.ush`, `SubsurfaceTiles.cpp` |  |

### Stage 3: Blur / Scattering (Core Math)

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Blur topology** | Single-pass 2D importance-sampled disc (8–128 samples); Fibonacci golden-angle spiral (deterministic) or R2 quasi-random (temporal noise mode) | **AFIS path:** single-pass 2D importance-sampled disk (up to 64 samples per pixel); **Separable path:** two-pass horizontal+vertical | Aligned |
| **Two-S approach** | Scalar S for sampling via `getSearchLightDiffuseScalingFactor(sssA)` where `sssA = max(sssAlbedo.r, g, b)`. Per-channel S3D for profile via `getSearchLightDiffuseScalingFactor3D(sssAlbedo)`. Three D values: profile D = `dmfp_cm / S3D`, center D = `vec3(scalarDmfp) / S3D`, sampling D = `scalarDmfp / scalarS`. | Scalar S from `GetScalingFactor(SurfaceAlbedo.a)`. S3D from `GetScalingFactor3D(SurfaceAlbedo.xyz)`. Profile D = `DMFP.xyz / S3D`, center D = `DMFP.a / S3D`, sampling D = `DMFP.a / S`. | **Aligned** |
| **Scalar albedo selection** | `max(sssAlbedo.r, max(sssAlbedo.g, sssAlbedo.b))` — matches UE's `.a` which is the albedo of the dominant DMFP channel | `SurfaceAlbedo.a` = albedo of dominant DMFP channel (packed by CPU in `SetupSurfaceAlbedoAndDiffuseMeanFreePath`) | **Aligned** |
| **Scaling factor polynomial** | `getSearchLightDiffuseScalingFactor(A)`: `3.5 + 100*(A - 0.33)^4` | `GetScalingFactor(A)`: `3.5 + 100*(A - 0.33)^4` | **Aligned** |
| **CDF / PDF / CDF inverse** | `burleyCdf(r, D)`, `burleyPdf(r, D)`, `burleyCdfInverse(xi, D)` — importance-sampled Burley profile | `GetCdf(r, D)`, `GetPdf(r, D)`, root-finding via `RadiusRootFindByApproximation()` | **Aligned** (Filament uses closed-form inverse; UE uses iterative root-finding — equivalent results) |
| **Profile evaluation** | Per-channel: `burleyProfile3(r, D)` where `D = dmfp_cm / S3D` | `GetDiffuseReflectProfileWithDiffuseMeanFreePath(r, L, S3D)` where `L = DMFP.xyz` | **Aligned** |
| **Center sample weight** | Per-channel CDF: `burleyCdf(halfPixelRadius, centerD)` where `centerD = vec3(scalarDmfp) / S3D`. Scalar CDF for sample range remapping: `burleyCdf(halfPixelRadius, dominantD)`. | `CalculateCenterSampleWeight()`: per-channel CDF with `D = DMFP.a / S3D`. `CalculateCenterSampleCdf()`: scalar CDF with `D = DMFP.a / S`. | **Aligned** |
| **Sample distribution** | CDF-based importance sampling: `xi` remapped to `[centerCdf, 1]`; `burleyCdfInverse(xi, dominantD)` returns world-space radius; converted to pixel offset via `projectedScale2D / depth` (per-axis for non-square viewports). Angle from Fibonacci spiral (deterministic) or R2 sequence (temporal). | **AFIS:** R2 quasi-random sequence for (radius, angle) pairs; radius via `RadiusRootFindByApproximation()`. **Separable:** precomputed kernel LUT. | **Aligned** |
| **Projected scale** | Per-axis `projectedScale2D = (0.5 * resolution * |projection diagonal|)` — correct for non-square viewports and asymmetric projection | Single `ProjectedScale` from projection matrix | **Aligned** (Filament slightly more general with 2D scale) |
| **Bilateral rejection** | 3D radius correction: `sqrt(worldDist² + deltaDepth²)` folds depth difference into scatter distance; Normal weight: `sqrt(dot(N_c, N_s) * 0.5 + 0.5)` using normals (1-tap or 5-tap per `fastSampleNormals`); Membership mask multiplication | Depth weight (configurable); Normal weight (optional, `r.SSS.Burley.BilateralFilterKernelFunctionType`); Profile ID rejection (samples from different profiles are rejected) | **Aligned** |
| **Normalization** | `scatteredDiffuse = totalDiffuse / max(totalWeight, 1e-4)` then `mix(scatteredDiffuse, centerDiffuse, centerCdfWeight)` | Same: accumulated weighted sum / total weight, then lerp with center sample via CDF weight (single application) | **Aligned** |
| **Energy normalization constant** | Not applied (missing `1/0.99995` factor) | `burley_re = 0.99995f`; profile divided by this | Negligible gap (0.005%) |
| **Temporal integration** | Optional R2 per-pixel per-frame jitter (`temporalNoise` toggle, seeded via `pcgHash(pixelCoord + frameId)`). No explicit temporal accumulation buffer — relies on downstream TAA. | Explicit per-pixel R2 sampling (frame-seeded) + exponential moving average across frames. Velocity-aware history rejection. | **Partial** — Filament has per-pixel jitter but no dedicated temporal accumulation/history buffer |
| **Adaptive sample count** | Not implemented. Fixed `sampleCount` (default 64, range 8–128). | Variance-based adaptive: history buffer tracks convergence; range 8-64; temporal accumulation with exponential weighting | Gap |
| **Mip-level optimization** | None; always samples at LOD 0 | Adaptive mip selection based on sample radius; `MIP_CONSTANT_FACTOR` reduces aliasing for far samples; mip generation triggered by tile count threshold | Gap |
| **Half-res path** | Not implemented | `r.SSS.HalfRes 1`: downscale, blur at half resolution, upsample+recombine; Burley can fallback to separable in half-res | Gap |
| **Profile-aware sampling** | Single view-global profile; per-pixel tint and distance via MRT but kernel shape controlled by view-level `falloffColor` | Per-pixel profile lookup; AFIS reads per-profile DMFP and scaling factors; profile ID cache (`R8_UINT` texture) accelerates repeated lookups | Gap |
| **Source files** | `sssBlur.mat` (fragment block) | `SubsurfaceBurleyNormalized.ush`, `BurleyNormalizedSSSCommon.ush`, `SeparableSSS.ush`, `PostProcessSubsurface.usf` |  |

### Stage 4: Recombine

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Specular preservation** | `centerSpecular = max(centerColor.rgb - centerSetupDiffuse, 0)`; added back after blur: `blurredDiffuse * centerAlbedo + centerSpecular` | `ReconstructLighting()` extracts `DiffuseAndSpecular`; specular added back: `SubsurfaceLighting * StoredBaseColor + ExtractedNonSubsurface` | Aligned |
| **Tint/lerp** | No tint/lerp step — blurred diffuse replaces original directly. Tint is baked into the per-channel D (DMFP) via `centerTint * scaledSd`, so scattering strength already varies per-channel. | `lerp(DiffuseAndSpecular.Diffuse, SSSColor, FadedTint)` where `FadedTint = Tint * LerpFactor`; LerpFactor from `ComputeFullResLerp()` in half-res mode | Aligned |
| **Base color multiply** | Albedo divided out before blur (samples read from `albedo` MRT), multiplied back at recombine (`blurredDiffuse * centerAlbedo`). Ensures blur operates in irradiance space. | Explicit: `SubsurfaceLighting * StoredBaseColor` at final output | **Aligned** |
| **Boundary color bleed** | Not implemented | `GetSubsurfaceProfileBoundaryColorBleed()` per profile; prevents color bleeding at SSS/non-SSS boundaries | Gap |
| **Opacity-weighted SSS fade** | Implemented: `mix(originalDiffuse, scatteredDiffuse, mask)` at recombine, where `mask = max(subsurfaceColor.rgb)` provides continuous [0,1] fade. Pixels with lower subsurface intensity get proportionally less scattering. | Per-profile opacity controls SSS intensity fade | **Aligned** |
| **Source files** | `sssBlur.mat` (recombine block) | `PostProcessSubsurface.usf` (`SubsurfaceRecombinePS`) |  |

### Stage 5: Transmission

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Model** | **Not implemented.** Thickness is stored in MRT normal buffer `.a` but not consumed by the blur pass. No transmission parameters exist in the current `SubsurfaceScatteringOptions`. | Full Burley transmission profile: `0.25 * A * (exp(-S*r) + 3*exp(-S*r/3))`; per-channel MFP; uses `GetPerpendicularScalingFactor3D()` for scaling; precomputed transmission profile (32 samples) with distance fade | Gap |
| **Thickness source** | Per-pixel from MRT normal buffer `.a` (material `thickness` property) — stored but not consumed by blur | Per-pixel from GBuffer `CustomData`; shadow map depth for transmission distance | Gap |
| **Source files** | `sssBlur.mat`, `surface_main.fs` | `BurleyNormalizedSSSCommon.ush` (`GetBurleyTransmissionProfile`), `TransmissionCommon.ush` |  |

### Stage 6: Debug / Visualization

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Debug modes** | Removed — no shader-level debug visualization. Use RenderDoc or external tools for buffer inspection. | `SubsurfaceVisualizePS`: checkerboard overlay showing SSS pixels; `r.SSS.Checkerboard` visualization; buffer inspection via RenderDoc / GPU profiler | Gap |
| **Sample app** | `sample_sss_burley.cpp`: material presets, ImGui controls for material properties (base color, roughness, metallic, reflectance), Burley params (falloffColor, scatteringDistance, dual specular), SSS options (sampleCount 8–128, temporalNoise, fastSampleNormals), gap analysis table (16-row comparison vs UE), comparison workflow (4 viewpoints × 1 artifact), performance benchmark (10 configurations) | Material editor preview; SSS profile asset visualization in editor; no standalone sample | N/A |
| **Source files** | `Options.h`, `sample_sss_burley.cpp`, `sssBlur.mat` | `PostProcessSubsurface.usf` (`SubsurfaceVisualizePS`), Editor UI |  |

### Stage 7: Pipeline Scheduling

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Render pass order** | Color pass (4 SSS MRTs + main color) → SSS disc blur (single pass) → TAA → other post-processing | GBuffer → Lighting → SSS setup → Tile classification → Indirect dispatch (Burley + Separable) → Recombine → TAA | Aligned |
| **Dispatch model** | Full-screen quad, single pass | **AFIS:** Compute shader with indirect dispatch; only SSS tiles processed. **Separable:** Pixel shader with indirect dispatch per tile | Gap |
| **Scalability** | `sampleCount` (8–128), `temporalNoise` toggle, `fastSampleNormals` toggle. Single full-res path only. | Configurable: `r.SSS.HalfRes`, `r.SSS.Burley.Quality`, `r.SSS.SampleSet`, `r.SSS.Scale`; per-profile implementation hint; subpixel threshold skip | Gap |
| **Source files** | `Renderer.cpp`, `PostProcessManager.cpp` | `PostProcessSubsurface.cpp`, `DeferredShadingRenderer.cpp` |  |

---

## Key Architectural Differences Summary

| Area | Filament Approach | Unreal Approach | Status |
|---|---|---|---|
| **Profile system** | View-global + per-pixel MRT (4 × RGBA16F) | Asset-based profile atlas (256 slots) | Gap |
| **FalloffColor mapping** | CPU-side polynomial mapping identical to UE | `MapFallOffColor2SurfaceAlbedoAndDiffuseMeanFreePath` | **Aligned** |
| **Two-S kernel math** | Scalar S for sampling, per-channel S3D for profile evaluation, three D values | Same two-S approach | **Aligned** |
| **Scalar albedo for sampling** | `max(sssAlbedo.r, g, b)` — matches UE's `.a` (dominant DMFP channel albedo) | `SurfaceAlbedo.a` packed by CPU | **Aligned** |
| **Bilateral filter** | 3D radius correction + normal weight (1-tap or 5-tap) | Same 3D correction + normal weight + profile ID rejection | **Aligned** (missing profile ID) |
| **Temporal noise** | Optional R2 per-pixel jitter (`temporalNoise` toggle); relies on TAA for accumulation | Built-in R2 + dedicated exponential moving average history buffer | **Partial** |
| **Tile classification** | None | Indirect dispatch with tile buffers | Gap |
| **Half-res** | Not available | Full pipeline support | Gap |
| **Boundary handling** | Not implemented | Per-profile BoundaryColorBleed | Gap |
| **Transmission** | Not implemented (thickness stored but unused) | Full Burley transmission profile | Gap |
| **Debug visualization** | Removed — no shader-level debug modes | Checkerboard overlay + buffer inspection | Gap |
| **Dead code** | **None** — all declared parameters are actively used | N/A | Clean |

## Remaining Gaps (Ranked by Visual Impact)

1. **Temporal accumulation history** — Filament now has per-pixel R2 jitter (`temporalNoise`), but lacks a dedicated temporal accumulation buffer with exponential moving average and velocity-aware history rejection. Relies on downstream TAA instead. This is the main remaining quality gap for scatter gradient smoothness.
2. **Per-pixel SSS masking via profile ID** — Blur applies to entire scene. Non-SSS pixels (sky, eyes) participate in scattering when near SSS surfaces. Membership mask helps but isn't as precise as profile ID rejection.
3. **Boundary color bleed attenuation** — Not implemented. Color can leak at SSS/non-SSS boundaries.
4. **Adaptive mip selection** — Always LOD 0. Far samples should read higher mip levels to reduce aliasing.
5. **Half-res blur path** — No performance fallback for mobile / low-end.
6. **Adaptive sample count** — Not implemented. Fixed sample count only (default 64).
7. **Tile classification / indirect dispatch** — Full-screen blur even when few SSS pixels exist.
8. **Transmission** — Thickness is stored in MRT but not consumed. No back-scattering through thin objects.
9. **Multi-profile support** — Per-pixel tint and distance vary via MRT, but kernel shape (falloffColor → sssAlbedo) is view-global. Multi-material scenes with different skin types share one kernel profile.

## SubsurfaceScatteringOptions (Current API)

```cpp
struct SubsurfaceScatteringOptions {
    bool enabled = false;
    uint8_t sampleCount = 64;
    float scatteringDistance = 1.0f;
    math::float3 subsurfaceColor = {0.8f, 0.3f, 0.2f};
    float worldUnitScale = 1.0f;
    math::float3 falloffColor = {1.0f, 0.37f, 0.3f};
    bool temporalNoise = false;
    bool fastSampleNormals = true;
};
```

## sssBlur.mat Parameters (All Active)

| Parameter | Type | Purpose |
|---|---|---|
| `color` | sampler2d | Color buffer input (with specular) |
| `depth` | sampler2d | Depth for 3D radius correction & bilateral depth culling |
| `normal` | sampler2d | Shading normal for bilateral rejection |
| `params` | sampler2d | Per-pixel Burley params (tint.rgb, scatteringDistance.a) |
| `albedo` | sampler2d | Surface albedo for recombine |
| `setupDiffuse` | sampler2d | Setup diffuse buffer (diffuse.rgb, mask.a) |
| `resolution` | float4 | (width, height, 1/width, 1/height) |
| `scatteringDistance` | float | View-level DMFP multiplier (cm) |
| `subsurfaceColor` | float3 | View-level blend tint (with DMFP ratios folded in) |
| `worldUnitScale` | float | Scene units → cm conversion |
| `cameraFar` | float | Far plane for depth culling (early exit) |
| `sampleCount` | int | Number of disc samples (8–128) |
| `projectedScale2D` | float2 | Pixels/world-unit at depth=1 (per axis) |
| `sssAlbedo` | float3 | Derived from falloffColor; DMFP ratio basis |
| `frameId` | int | Per-frame jitter seed (temporal noise mode) |
| `temporalNoise` | bool | Toggle R2 jitter vs deterministic Fibonacci spiral |
| `fastSampleNormals` | bool | Toggle 1-tap vs 5-tap normal sampling |

## MRT Buffer Layout

| Buffer | Format | Contents |
|---|---|---|
| `sssDiffuse` (MRT 1) | RGBA16F | Diffuse RGB + SSS membership mask (A) |
| `sssNormal` (MRT 2) | RGBA16F | View-space shading normal XYZ + thickness (A) |
| `sssParams` (MRT 3) | RGBA16F | subsurfaceColor RGB + scatteringDistance (A) |
| `sssAlbedo` (MRT 4) | RGBA16F | Surface albedo RGB + radiusScale (A) |

## Producing the Output

When generating the comparison:

1. **Read current source first.** Do not rely on this skill's snapshot alone. Verify key structs, function signatures, and pipeline stages against the actual files listed above.
2. **Use the table format above.** The user expects a structured, stage-by-stage comparison.
3. **Cite file:line where possible.** This makes the comparison actionable.
4. **Flag stale information.** If a gap listed here has been closed since the skill was written, note the change.
5. **Keep opinions out.** State what each engine does and where the code lives. Let the user draw conclusions about priority.
