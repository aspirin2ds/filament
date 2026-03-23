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
| **Profile system** | Global view-level `SubsurfaceScatteringOptions` struct; per-material `subsurfaceColor` + `scatteringDistance` written to MRT | `USubsurfaceProfile` asset with `FSubsurfaceProfileStruct`; up to 256 profiles stored in a texture atlas (`FSubsurfaceProfileTexture`) | Gap |
| **FalloffColor → SurfaceAlbedo + DMFP** | CPU-side (`PostProcessManager.cpp:1240-1256`): `falloffColor` mapped to `sssAlbedo` and `dmfpRaw` via UE's `MapFallOffColor2SurfaceAlbedoAndDiffuseMeanFreePath` polynomial. `sssAlbedo = 0.906*fc + 0.00004`, DMFP via degree-5 polynomial. DMFP factored into `dmfpScale` (max channel) and `dmfpRatios`. | CPU-side (`BurleyNormalizedSSS.cpp`): identical `MapFallOffColor2SurfaceAlbedoAndDiffuseMeanFreePath` polynomial mapping. `SurfaceAlbedo.a` packed as albedo of dominant DMFP channel. | **Aligned** |
| **Scattering distance** | `falloffColor` → `dmfpRaw` (degree-5 polynomial) → `dmfpScale * dmfpRatios`. `options.scatteringDistance * dmfpScale` passed as global scale; `dmfpRatios` folded into `subsurfaceColor`. | Per-profile `MeanFreePathColor` (RGB) + `MeanFreePathDistance` (float, cm); combined into per-channel DMFP stored in profile texture | **Aligned** (math matches; Filament uses view-global rather than per-profile) |
| **World unit scale** | View-global `worldUnitScale` (default 1.0) | Per-profile `WorldUnitScale` (default 0.1, in cm) | Aligned |
| **Tint** | View-global `subsurfaceColor` (float3); per-material `subsurfaceColor` written to `sssParams.rgb`. DMFP ratios folded in: `subsurfaceColor * dmfpRatios`. | Per-profile `Tint` (RGBA); controls lerp between original and scattered | Aligned |
| **Boundary color bleed** | Declared in `SubsurfaceScatteringOptions` and passed to shader, but **not read or applied** by `sssBlur.mat` — dead parameter | Per-profile `BoundaryColorBleed` (RGBA); applied during recombine to prevent color leaking at SSS boundaries | Gap |
| **Transmission params** | Declared in `SubsurfaceScatteringOptions` (`extinctionScale`, `normalScale`, `scatteringDistribution`, `ior`, `transmissionTintColor`, `transmissionMFPScaleFactor`) and passed to shader, but **not read or applied** by `sssBlur.mat` — dead parameters. UI sliders exist in sample app but have no effect. | Per-profile: `ExtinctionScale`, `NormalScale`, `ScatteringDistribution`, `IOR`, `TransmissionTintColor` | Gap |
| **Dual specular** | Per-material: `roughness0` (0.75), `roughness1` (1.3), `lobeMix` (0.85) | Per-profile: `Roughness0` (0.75), `Roughness1` (1.3), `LobeMix` (0.85) | Aligned |
| **Adaptive sampling** | Declared in `SubsurfaceScatteringOptions` (`adaptiveSampleCount`, `minSampleCount`) and passed to shader, but **not read or applied** by `sssBlur.mat` — dead parameters | Variance-based adaptive: history buffer tracks convergence; range 8-64; `r.SSS.Burley.NumSamplesOverride` for manual control | Gap |
| **Implementation hint** | Single path (separable blur only; disc mode available via `discSampling` bool) | Per-profile choice: `SIH_AFIS` (importance-sampled Burley) or `SIH_Separable` (separable filter) | Aligned |
| **Profile ID** | No profile ID system; all SSS pixels share blur parameters from view + per-pixel MRT values | GBuffer encodes profile ID (0-255); blur reads per-profile kernel weights from profile texture | Gap |
| **Source files** | `Options.h`, `PostProcessManager.cpp`, `surface_material_inputs.fs`, `surface_main.fs` | `SubsurfaceProfile.h`, `SubsurfaceProfileCommon.ush`, `BurleyNormalizedSSS.cpp` |  |

### Stage 2: Setup / Decomposition (Color Pass)

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Diffuse/specular separation** | Explicit MRT: `g_sssDiffuse` accumulates only diffuse lighting (specular excluded); written to `fragColor1.rgb` | Checkerboard or full-res: `ReconstructLighting()` separates diffuse and specular from composited scene color using GBuffer data | Aligned |
| **Setup buffer contents** | MRT 1: diffuse RGB + membership mask (A); MRT 2: view-normal XYZ + thickness (A); MRT 3: subsurfaceColor RGB + scatteringDistance (A); MRT 4: surface albedo RGB | No separate setup buffer; blur reads scene color directly; GBuffer provides normals, profile ID, thickness, base color | Different approach, equivalent result |
| **SSS membership** | `g_sssMask` = max channel of `subsurfaceColor`; written to `fragColor1.a` | GBuffer shading model ID checked (`UseSubsurfaceProfile()`); stencil bit marks SSS pixels for tile classification | Aligned |
| **Normal storage** | Dedicated MRT: shaded view-space normal (matches material normal map) + thickness | GBuffer world normal; no separate SSS normal buffer | Aligned |
| **Tile classification** | None; blur applied to all pixels, early-out on mask/depth | Indirect dispatch: setup pass classifies tiles as Burley or Separable into dispatch buffers; only SSS tiles are processed | Gap |
| **Source files** | `surface_shading_model_subsurface_burley.fs`, `surface_main.fs`, `RendererUtils.cpp` | `PostProcessSubsurface.usf` (setup pass), `SubsurfaceBurleyNormalized.ush`, `SubsurfaceTiles.cpp` |  |

### Stage 3: Blur / Scattering (Core Math)

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Blur topology** | Two-pass separable (horizontal then vertical); optional single-pass 2D disc mode (`discSampling`) | **AFIS path:** single-pass 2D importance-sampled disk (up to 64 samples per pixel); **Separable path:** two-pass horizontal+vertical | Aligned |
| **Two-S approach** | **Aligned.** Scalar S for sampling via `getSearchLightDiffuseScalingFactor(sssA)` where `sssA = max(sssAlbedo.r, g, b)`. Per-channel S3D for profile via `getSearchLightDiffuseScalingFactor3D(sssAlbedo)`. Three D values: profile D = `dmfp_cm / S3D`, center D = `vec3(scalarDmfp) / S3D`, sampling D = `scalarDmfp / scalarS`. | Scalar S from `GetScalingFactor(SurfaceAlbedo.a)`. S3D from `GetScalingFactor3D(SurfaceAlbedo.xyz)`. Profile D = `DMFP.xyz / S3D`, center D = `DMFP.a / S3D`, sampling D = `DMFP.a / S`. | **Aligned** |
| **Scalar albedo selection** | `max(sssAlbedo.r, max(sssAlbedo.g, sssAlbedo.b))` — matches UE's `.a` which is the albedo of the dominant DMFP channel. Correctness relies on both being derived from same monotonic `falloffColor`. | `SurfaceAlbedo.a` = albedo of dominant DMFP channel (packed by CPU in `SetupSurfaceAlbedoAndDiffuseMeanFreePath`) | **Aligned** |
| **Scaling factor polynomial** | `getSearchLightDiffuseScalingFactor(A)`: `3.5 + 100*(A - 0.33)^4` | `GetScalingFactor(A)`: `3.5 + 100*(A - 0.33)^4` | **Aligned** |
| **CDF / PDF / CDF inverse** | `burleyCdf(r, D)`, `burleyPdf(r, D)`, `burleyCdfInverse(xi, D)` — importance-sampled Burley profile | `GetCdf(r, D)`, `GetPdf(r, D)`, root-finding via `RadiusRootFindByApproximation()` | **Aligned** (Filament uses closed-form inverse; UE uses iterative root-finding — equivalent results) |
| **Profile evaluation** | Per-channel: `burleyProfile3(r, D)` where `D = dmfp_cm / S3D` | `GetDiffuseReflectProfileWithDiffuseMeanFreePath(r, L, S3D)` where `L = DMFP.xyz` | **Aligned** |
| **Center sample weight** | Per-channel CDF: `burleyCdf(halfPixelRadius, centerD)` where `centerD = vec3(scalarDmfp) / S3D`. Scalar CDF for sample range remapping: `burleyCdf(halfPixelRadius, dominantD)`. | `CalculateCenterSampleWeight()`: per-channel CDF with `D = DMFP.a / S3D`. `CalculateCenterSampleCdf()`: scalar CDF with `D = DMFP.a / S`. | **Aligned** |
| **Sample distribution** | CDF-based: `xi` remapped to `[centerCdf, 1]`; `burleyCdfInverse(xi, dominantD)` returns world-space radius; converted to pixel offset via `projectedScale / depth`. Disc mode: Fibonacci golden angle spiral. | **AFIS:** R2 quasi-random sequence for (radius, angle) pairs; radius via `RadiusRootFindByApproximation()`. **Separable:** precomputed kernel LUT. | Aligned (deterministic vs quasi-random — see temporal integration) |
| **Bilateral rejection** | 3D radius correction: `sqrt(worldDist² + deltaDepth²)` folds depth difference into scatter distance (matches UE `SubsurfaceBurleyNormalized.ush:944-945`); Normal weight: `sqrt(dot(N_c, N_s) * 0.5 + 0.5)` using smoothed normals; Membership mask multiplication | Depth weight (configurable); Normal weight (optional, `r.SSS.Burley.BilateralFilterKernelFunctionType`); Profile ID rejection (samples from different profiles are rejected) | **Aligned** |
| **Normalization** | Disc: `scatteredDiffuse = totalDiffuse / max(totalWeight, 1e-4)` then `mix(scatteredDiffuse, centerDiffuse, centerCdfWeight)`. Separable: center excluded from both H and V accumulations; center CDF lerp applied once at recombine pass using original (pre-blur) center diffuse. | Same: accumulated weighted sum / total weight, then lerp with center sample via CDF weight (single application) | **Aligned** |
| **Energy normalization constant** | Not applied (missing `1/0.99995` factor) | `burley_re = 0.99995f`; profile divided by this | Negligible gap (0.005%) |
| **Temporal integration** | **None.** Deterministic Fibonacci spiral (disc) or fixed separable samples. Relies on downstream TAA. | Explicit per-pixel random sampling (R2 sequence + frame number seed) + exponential moving average across frames. Velocity-aware history rejection. | **Gap — main visual quality difference** |
| **Adaptive sample count** | Fixed `sampleCount` from view options (default 11); `adaptiveSampleCount` and `minSampleCount` declared but **not wired** | Variance-based adaptive: history buffer tracks convergence; range 8-64; temporal accumulation with exponential weighting | Gap |
| **Mip-level optimization** | None; always samples at LOD 0 | Adaptive mip selection based on sample radius; `MIP_CONSTANT_FACTOR` reduces aliasing for far samples; mip generation triggered by tile count threshold | Gap |
| **Half-res path** | Not implemented | `r.SSS.HalfRes 1`: downscale, blur at half resolution, upsample+recombine; Burley can fallback to separable in half-res | Gap |
| **Profile-aware sampling** | Single profile; all SSS pixels share one kernel shape | Per-pixel profile lookup; AFIS reads per-profile DMFP and scaling factors; profile ID cache (`R8_UINT` texture) accelerates repeated lookups | Gap |
| **Source files** | `sssBlur.mat` (fragment block) | `SubsurfaceBurleyNormalized.ush`, `BurleyNormalizedSSSCommon.ush`, `SeparableSSS.ush`, `PostProcessSubsurface.usf` |  |

### Stage 4: Recombine

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Specular preservation** | `centerSpecular = max(centerColor.rgb - centerSetupDiffuse, 0)`; added back after blur: `blurredDiffuse * centerAlbedo + centerSpecular` | `ReconstructLighting()` extracts `DiffuseAndSpecular`; specular added back: `SubsurfaceLighting * StoredBaseColor + ExtractedNonSubsurface` | Aligned |
| **Tint/lerp** | No tint/lerp step — blurred diffuse replaces original directly. Tint is baked into the per-channel D (DMFP) via `centerTint * scaledSd`, so scattering strength already varies per-channel. | `lerp(DiffuseAndSpecular.Diffuse, SSSColor, FadedTint)` where `FadedTint = Tint * LerpFactor`; LerpFactor from `ComputeFullResLerp()` in half-res mode | Aligned |
| **Base color multiply** | Albedo divided out before blur (`diffuseP /= sampleSurfaceAlbedo(uvP)` in H-pass), multiplied back at recombine (`blurredDiffuse * centerAlbedo`). Ensures blur operates in irradiance space. | Explicit: `SubsurfaceLighting * StoredBaseColor` at final output | **Aligned** |
| **Boundary color bleed** | Not applied (parameter declared but dead) | `GetSubsurfaceProfileBoundaryColorBleed()` per profile; prevents color bleeding at SSS/non-SSS boundaries | Gap |
| **Opacity-weighted SSS fade** | Not implemented (binary SSS mask) | Per-profile opacity controls SSS intensity fade | Gap |
| **Source files** | `sssBlur.mat` (recombine block, passIndex != 0) | `PostProcessSubsurface.usf` (`SubsurfaceRecombinePS`) |  |

### Stage 5: Transmission

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Model** | **Not implemented.** Parameters (`ior`, `extinctionScale`, `normalScale`, `scatteringDistribution`, `transmissionTintColor`, `transmissionMFPScaleFactor`) are declared in `SubsurfaceScatteringOptions`, passed from `PostProcessManager` to `sssBlur.mat`, and exposed in the sample app UI, but the shader **never reads them**. Helper functions `getPerpendicularScalingFactor3D()` and `getMfpFromDmfpApprox()` exist in the shader but are **unreferenced dead code**. | Full Burley transmission profile: `0.25 * A * (exp(-S*r) + 3*exp(-S*r/3))`; per-channel MFP; uses `GetPerpendicularScalingFactor3D()` for scaling; precomputed transmission profile (32 samples) with distance fade | Gap |
| **Thickness source** | Per-pixel from MRT normal buffer `.a` (material `thickness` property) — stored but not consumed by blur | Per-pixel from GBuffer `CustomData`; shadow map depth for transmission distance | Gap |
| **Source files** | `sssBlur.mat` (dead params only), `Options.h` (dead fields) | `BurleyNormalizedSSSCommon.ush` (`GetBurleyTransmissionProfile`), `TransmissionCommon.ush` |  |

### Stage 6: Debug / Visualization

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Debug modes** | 2 modes: `NONE` (normal rendering), `SCATTERING` (shows blurred diffuse x albedo without specular, `debugMode == 1`). Enum `SubsurfaceScatteringDebugMode` in `Options.h`. | `SubsurfaceVisualizePS`: checkerboard overlay showing SSS pixels; `r.SSS.Checkerboard` visualization; buffer inspection via render doc / GPU profiler | Gap |
| **Sample app** | `sample_sss_burley.cpp`: ImGui controls for material (base color, roughness, metallic, reflectance), SSS (thickness, MFP distance/color, world unit scale, falloffColor), dual specular, blur pass (enable, sample count), light, debug views, and capture. Has dead UI sections for IOR, Transmission, Boundary, and Adaptive Sampling that map to dead Options.h fields. | Material editor preview; SSS profile asset visualization in editor; no standalone sample | N/A |
| **Source files** | `Options.h`, `sample_sss_burley.cpp` | `PostProcessSubsurface.usf` (`SubsurfaceVisualizePS`), Editor UI |  |

### Stage 7: Pipeline Scheduling

| Aspect | Filament | Unreal Engine | Status |
|---|---|---|---|
| **Render pass order** | Color pass (MRT) -> SSS blur (H+V) -> TAA -> other post-processing | GBuffer -> Lighting -> SSS setup -> Tile classification -> Indirect dispatch (Burley + Separable) -> Recombine -> TAA | Aligned |
| **Dispatch model** | Full-screen quad for each blur pass | **AFIS:** Compute shader with indirect dispatch; only SSS tiles processed. **Separable:** Pixel shader with indirect dispatch per tile | Gap |
| **Scalability** | Fixed: single full-res path | Configurable: `r.SSS.HalfRes`, `r.SSS.Burley.Quality`, `r.SSS.SampleSet`, `r.SSS.Scale`; per-profile implementation hint; subpixel threshold skip | Gap |
| **Source files** | `Renderer.cpp`, `PostProcessManager.cpp` | `PostProcessSubsurface.cpp`, `DeferredShadingRenderer.cpp` |  |

---

## Key Architectural Differences Summary

| Area | Filament Approach | Unreal Approach | Status |
|---|---|---|---|
| **Profile system** | View-global + per-pixel MRT | Asset-based profile atlas (256 slots) | Gap |
| **FalloffColor mapping** | CPU-side polynomial mapping identical to UE (`PostProcessManager.cpp:1240-1256`) | `MapFallOffColor2SurfaceAlbedoAndDiffuseMeanFreePath` | **Aligned** |
| **Two-S kernel math** | Scalar S for sampling, per-channel S3D for profile evaluation, three D values (profile/center/sampling) | Same two-S approach | **Aligned** |
| **Scalar albedo for sampling** | `max(sssAlbedo.r, g, b)` — matches UE's `.a` (dominant DMFP channel albedo) | `SurfaceAlbedo.a` packed by CPU | **Aligned** |
| **Bilateral filter** | 3D radius correction + normal weight | Same 3D correction + normal weight + profile ID rejection | **Aligned** (missing profile ID) |
| **Tile classification** | None | Indirect dispatch with tile buffers | Gap |
| **Temporal integration** | Relies on TAA downstream | Built-in per-pixel random + exponential moving average | **Gap — main visual quality difference** |
| **Half-res** | Not available | Full pipeline support | Gap |
| **Boundary handling** | Parameter declared but dead (not read by shader) | Per-profile BoundaryColorBleed | Gap |
| **Transmission** | Not implemented (params declared but dead) | Full Burley transmission profile | Gap |
| **Dead code** | 9 dead params, 2 dead shader functions, 4 dead UI sections | N/A | Cleanup needed |

## Remaining Gaps (Ranked by Visual Impact)

1. **Temporal integration** — Main visual quality gap. UE uses per-pixel R2 quasi-random sampling with frame-seeded rotation + exponential moving average. Filament uses deterministic Fibonacci spiral with no temporal accumulation. This causes thicker/more concentrated scatter bands in Filament vs UE's smoother gradients.
2. **Per-pixel SSS masking via profile ID** — Blur applies to entire scene. Non-SSS pixels (sky, eyes) participate in scattering when near SSS surfaces.
3. **Boundary color bleed attenuation** — Parameter exists but is dead. Color can leak at SSS/non-SSS boundaries.
4. **Opacity-weighted SSS fade** — Binary SSS mask only. No gradual fade-out of scattering effect.
5. **Adaptive mip selection** — Always LOD 0. Far samples should read higher mip levels to reduce aliasing.
6. **Half-res blur path** — No performance fallback for mobile / low-end.
7. **Adaptive sample count** — Parameters declared but dead. Fixed sample count only.
8. **Tile classification / indirect dispatch** — Full-screen blur even when few SSS pixels exist.
9. **Transmission** — Parameters and helper functions exist as dead code. No back-scattering through thin objects.

## Dead Code Inventory (Filament)

The following parameters and code exist in the Filament SSS pipeline but are **not functionally wired** — they are declared, passed through the CPU side, and exposed in the sample app UI, but the shader (`sssBlur.mat`) never reads them:

| Dead Parameter | Declared In | Passed In | Shader Status |
|---|---|---|---|
| `ior` | `Options.h` | `PostProcessManager.cpp` | Declared in `.mat` params but never read in fragment block |
| `extinctionScale` | `Options.h` | `PostProcessManager.cpp` | Same |
| `normalScale` | `Options.h` | `PostProcessManager.cpp` | Same |
| `scatteringDistribution` | `Options.h` | `PostProcessManager.cpp` | Same |
| `transmissionTintColor` | `Options.h` | `PostProcessManager.cpp` | Same |
| `transmissionMFPScaleFactor` | `Options.h` | `PostProcessManager.cpp` | Same |
| `boundaryColorBleed` | `Options.h` | `PostProcessManager.cpp` | Same |
| `adaptiveSamples` | `Options.h` | `PostProcessManager.cpp` | Same |
| `minSampleCount` | `Options.h` | `PostProcessManager.cpp` | Same |

Dead shader functions in `sssBlur.mat`:
- `getPerpendicularScalingFactor3D()` — declared but never called
- `getMfpFromDmfpApprox()` — declared but never called

Dead sample app UI sections:
- IOR slider (maps to dead `ior`)
- Transmission collapsing section (maps to dead transmission params)
- Boundary collapsing section (maps to dead `boundaryColorBleed`)
- Adaptive Sample Count checkbox + Min Samples slider (maps to dead adaptive params)

## Producing the Output

When generating the comparison:

1. **Read current source first.** Do not rely on this skill's snapshot alone. Verify key structs, function signatures, and pipeline stages against the actual files listed above.
2. **Use the table format above.** The user expects a structured, stage-by-stage comparison.
3. **Cite file:line where possible.** This makes the comparison actionable.
4. **Flag stale information.** If a gap listed here has been closed since the skill was written, note the change.
5. **Keep opinions out.** State what each engine does and where the code lives. Let the user draw conclusions about priority.
