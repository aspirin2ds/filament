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

## Comparison Structure

Produce the comparison using the following stage-by-stage format. For each stage, show what each engine does, where the code lives, and note key differences.

---

### Stage 1: Profile / Parameter Definition

| Aspect | Filament | Unreal Engine |
|---|---|---|
| **Profile system** | Global view-level `SubsurfaceScatteringOptions` struct; per-material `subsurfaceColor` + `scatteringDistance` written to MRT | `USubsurfaceProfile` asset with `FSubsurfaceProfileStruct`; up to 256 profiles stored in a texture atlas (`FSubsurfaceProfileTexture`) |
| **Surface albedo** | Per-pixel from material diffuse color, written to `sssAlbedo` MRT attachment | Per-profile `SurfaceAlbedo` (RGBA) stored in profile texture row; also available per-pixel via GBuffer `StoredBaseColor` |
| **Scattering distance** | Per-material `scatteringDistance` (float) written to `sssParams.a`; view-global `scatteringDistance` acts as multiplier | Per-profile `MeanFreePathColor` (RGB) + `MeanFreePathDistance` (float, cm); combined into per-channel DMFP stored in profile texture |
| **World unit scale** | View-global `worldUnitScale` (default 1.0) | Per-profile `WorldUnitScale` (default 0.1, in cm) |
| **Tint** | View-global `subsurfaceColor` (float3); per-material `subsurfaceColor` written to `sssParams.rgb` | Per-profile `Tint` (RGBA); controls lerp between original and scattered |
| **Boundary color bleed** | Not implemented (tracked in sample metadata only) | Per-profile `BoundaryColorBleed` (RGBA); applied during recombine to prevent color leaking at SSS boundaries |
| **Transmission params** | View-global: `extinctionScale`, `normalScale`, `scatteringDistribution`, `ior`, `transmissionTintColor` | Per-profile: `ExtinctionScale`, `NormalScale`, `ScatteringDistribution`, `IOR`, `TransmissionTintColor` |
| **Dual specular** | Per-material: `roughness0` (0.75), `roughness1` (1.3), `lobeMix` (0.85) | Per-profile: `Roughness0` (0.75), `Roughness1` (1.3), `LobeMix` (0.85) |
| **Implementation hint** | Single path (separable blur only) | Per-profile choice: `SIH_AFIS` (importance-sampled Burley) or `SIH_Separable` (separable filter) |
| **Profile ID** | No profile ID system; all SSS pixels share blur parameters from view + per-pixel MRT values | GBuffer encodes profile ID (0-255); blur reads per-profile kernel weights from profile texture |
| **Source files** | `Options.h`, `surface_material_inputs.fs`, `surface_main.fs` | `SubsurfaceProfile.h`, `SubsurfaceProfileCommon.ush` |

### Stage 2: Setup / Decomposition (Color Pass)

| Aspect | Filament | Unreal Engine |
|---|---|---|
| **Diffuse/specular separation** | Explicit MRT: `g_sssDiffuse` accumulates only diffuse lighting (specular excluded); written to `fragColor1.rgb` | Checkerboard or full-res: `ReconstructLighting()` separates diffuse and specular from composited scene color using GBuffer data |
| **Setup buffer contents** | MRT 1: diffuse RGB + membership mask (A); MRT 2: view-normal XYZ + thickness (A); MRT 3: subsurfaceColor RGB + scatteringDistance (A); MRT 4: surface albedo RGB | No separate setup buffer; blur reads scene color directly; GBuffer provides normals, profile ID, thickness, base color |
| **SSS membership** | `g_sssMask` = max channel of `subsurfaceColor`; written to `fragColor1.a` | GBuffer shading model ID checked (`UseSubsurfaceProfile()`); stencil bit marks SSS pixels for tile classification |
| **Normal storage** | Dedicated MRT: shaded view-space normal (matches material normal map) + thickness | GBuffer world normal; no separate SSS normal buffer |
| **Tile classification** | None; blur applied to all pixels, early-out on mask/depth | Indirect dispatch: setup pass classifies tiles as Burley or Separable into dispatch buffers; only SSS tiles are processed |
| **Source files** | `surface_shading_model_subsurface_burley.fs`, `surface_main.fs`, `RendererUtils.cpp` | `PostProcessSubsurface.usf` (setup pass), `SubsurfaceBurleyNormalized.ush`, `SubsurfaceTiles.cpp` |

### Stage 3: Blur / Scattering

| Aspect | Filament | Unreal Engine |
|---|---|---|
| **Blur topology** | Two-pass separable (horizontal then vertical) | **AFIS path:** single-pass 2D importance-sampled disk (up to 64 samples per pixel); **Separable path:** two-pass horizontal+vertical |
| **Kernel function** | `burleyProfile3(r, D)` evaluated analytically per tap; per-channel D from albedo-scaled DMFP | **AFIS:** CDF inverse root-finding to sample radii from Burley profile; PDF weighting. **Separable:** precomputed kernel LUT from `ComputeMirroredBSSSKernel()` stored in profile texture |
| **Sample distribution** | Uniform quadratic spacing: `t = i/halfSamples`, `samplePixels = effectiveRadius * t^2` | **AFIS:** R2 quasi-random sequence for (radius, angle) pairs; radius via `RadiusRootFindByApproximation()`. **Separable:** fixed LUT offsets |
| **Bilateral rejection** | Depth weight: `1 - (depthDiff/threshold)^2`; Normal weight: `sqrt(dot(N_c, N_s) * 0.5 + 0.5)`; Membership mask multiplication | Depth weight (configurable); Normal weight (optional, `r.SSS.Burley.BilateralFilterKernelFunctionType`); Profile ID rejection (samples from different profiles are rejected) |
| **Adaptive sample count** | Fixed `sampleCount` from view options (default 11) | Variance-based adaptive: history buffer tracks convergence; range 8-64; temporal accumulation with exponential weighting; `r.SSS.Burley.NumSamplesOverride` for manual control |
| **Temporal integration** | None; blur applied before TAA, relies on TAA to resolve noise | Explicit temporal reprojection in Burley pass; velocity-aware history rejection; exponential blending with prior frame |
| **Mip-level optimization** | None; always samples at LOD 0 | Adaptive mip selection based on sample radius; `MIP_CONSTANT_FACTOR` reduces aliasing for far samples; mip generation triggered by tile count threshold |
| **Half-res path** | Not implemented | `r.SSS.HalfRes 1`: downscale, blur at half resolution, upsample+recombine; Burley can fallback to separable in half-res |
| **Profile-aware sampling** | Single profile; all SSS pixels share one kernel shape | Per-pixel profile lookup; AFIS reads per-profile DMFP and scaling factors; profile ID cache (`R8_UINT` texture) accelerates repeated lookups |
| **Source files** | `sssBlur.mat` (fragment block) | `SubsurfaceBurleyNormalized.ush`, `SeparableSSS.ush`, `PostProcessSubsurface.usf` |

### Stage 4: Recombine

| Aspect | Filament | Unreal Engine |
|---|---|---|
| **Specular preservation** | `centerSpecular = centerColor.rgb - centerSetupDiffuse`; added back after blur: `finalDiffuse + centerSpecular` | `ReconstructLighting()` extracts `DiffuseAndSpecular`; specular added back: `SubsurfaceLighting * StoredBaseColor + ExtractedNonSubsurface` |
| **Tint/lerp** | Per-pixel `fadedTint` based on scatterAmount, CDF falloff, detail gate, face boost; `mix(centerSetupLighting, blurredDiffuse, fadedTint)` | `lerp(DiffuseAndSpecular.Diffuse, SSSColor, FadedTint)` where `FadedTint = Tint * LerpFactor`; LerpFactor from `ComputeFullResLerp()` in half-res mode |
| **Base color multiply** | Implicit: albedo division/multiplication during blur; `finalScatterLighting * centerAlbedo` at output | Explicit: `SubsurfaceLighting * StoredBaseColor` at final output |
| **Boundary color bleed** | Not applied | `GetSubsurfaceProfileBoundaryColorBleed()` per profile; prevents color bleeding at SSS/non-SSS boundaries |
| **Terminator handling** | Custom terminator window: `terminatorAnchor * litFalloff * silhouetteSuppression`; controls scatter visibility at light/shadow boundary | No explicit terminator handling; relies on TAA and profile-level tuning |
| **High-frequency detail** | `softenedResidual = highFrequencyResidual * mix(0.05, 0.14, faceBoost)`; preserves small amount of pre-blur detail | Not explicitly preserved; quality level controls sharpness of reconstruction |
| **Source files** | `sssBlur.mat` (recombine block, passIndex != 0) | `PostProcessSubsurface.usf` (`SubsurfaceRecombinePS`) |

### Stage 5: Transmission

| Aspect | Filament | Unreal Engine |
|---|---|---|
| **Model** | Approximate: `silhouette * backlight * thinRegion * transmissionStrength * transmissionAttenuation`; phase function from `scatteringDistribution`; IOR-based Fresnel; extinction along optical path | Full Burley transmission profile: `0.25 * A * (exp(-S*r) + 3*exp(-S*r/3))`; per-channel MFP; uses `GetPerpendicularScalingFactor3D()` for scaling; precomputed transmission profile (32 samples) with distance fade |
| **Thickness source** | Per-pixel from MRT normal buffer `.a` (material `thickness` property) | Per-pixel from GBuffer `CustomData`; shadow map depth for transmission distance |
| **Transmission tint** | `transmissionTint = centerTint * transmissionTintColor` (view-global) | Per-profile `TransmissionTintColor`; applied to transmission profile result |
| **Normal blending** | Blends geometric and shaded normals: `mix(macroNoL, detailNoL, 0.35 * sqrt(normalScale))` | Configurable `NormalScale` per profile; blends between geometric and shading normal |
| **Source files** | `sssBlur.mat` (transmission block) | `BurleyNormalizedSSSCommon.ush` (`GetBurleyTransmissionProfile`), `TransmissionCommon.ush` |

### Stage 6: Debug / Visualization

| Aspect | Filament | Unreal Engine |
|---|---|---|
| **Debug modes** | 8 modes: NONE, MEMBERSHIP, INFLUENCE, PRE_BLUR_DIFFUSE, POST_BLUR_DIFFUSE, TERMINATOR_WINDOW, BAND_MASK, TRANSMISSION | `SubsurfaceVisualizePS`: checkerboard overlay showing SSS pixels; `r.SSS.Checkerboard` visualization; buffer inspection via render doc / GPU profiler |
| **Sample app** | `sample_sss_burley.cpp`: 4 presets (Unreal Reference, Skin, Marble, Wax); 4 viewpoints; ImGui controls; deterministic capture; markdown report generation | Material editor preview; SSS profile asset visualization in editor; no standalone sample |
| **Source files** | `Options.h`, `sample_sss_burley.cpp` | `PostProcessSubsurface.usf` (`SubsurfaceVisualizePS`), Editor UI |

### Stage 7: Pipeline Scheduling

| Aspect | Filament | Unreal Engine |
|---|---|---|
| **Render pass order** | Color pass (MRT) -> SSS blur (H+V) -> TAA -> other post-processing | GBuffer -> Lighting -> SSS setup -> Tile classification -> Indirect dispatch (Burley + Separable) -> Recombine -> TAA |
| **Dispatch model** | Full-screen quad for each blur pass | **AFIS:** Compute shader with indirect dispatch; only SSS tiles processed. **Separable:** Pixel shader with indirect dispatch per tile |
| **Scalability** | Fixed: single full-res path | Configurable: `r.SSS.HalfRes`, `r.SSS.Burley.Quality`, `r.SSS.SampleSet`, `r.SSS.Scale`; per-profile implementation hint; subpixel threshold skip |
| **Source files** | `Renderer.cpp`, `PostProcessManager.cpp` | `PostProcessSubsurface.cpp`, `DeferredShadingRenderer.cpp` |

---

## Key Architectural Differences Summary

| Area | Filament Approach | Unreal Approach | Impact |
|---|---|---|---|
| **Profile system** | View-global + per-pixel MRT | Asset-based profile atlas (256 slots) | Filament cannot mix different SSS materials with different blur radii in one scene without manual MRT encoding |
| **Blur algorithm** | Separable bilateral (fixed samples) | AFIS importance sampling (adaptive) + separable fallback | Unreal converges faster with fewer samples; handles varying radii per-pixel; temporal accumulation reduces per-frame cost |
| **Tile classification** | None | Indirect dispatch with tile buffers | Unreal skips non-SSS regions entirely; Filament processes every pixel (early-out on mask helps, but not equivalent) |
| **Temporal integration** | Relies on TAA downstream | Built-in temporal reprojection in blur pass | Unreal's blur is temporally stable without TAA; Filament's blur can show frame-to-frame noise |
| **Half-res** | Not available | Full pipeline support | Unreal has a performance fallback for mobile / low-end |
| **Boundary handling** | No boundary color bleed | Per-profile BoundaryColorBleed | Filament may show color leaking at SSS/non-SSS material boundaries |

## Producing the Output

When generating the comparison:

1. **Read current source first.** Do not rely on this skill's snapshot alone. Verify key structs, function signatures, and pipeline stages against the actual files listed above.
2. **Use the table format above.** The user expects a structured, stage-by-stage comparison.
3. **Cite file:line where possible.** This makes the comparison actionable.
4. **Flag stale information.** If a gap listed here has been closed since the skill was written, note the change.
5. **Keep opinions out.** State what each engine does and where the code lives. Let the user draw conclusions about priority.
