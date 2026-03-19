# Burley SSS Parity Report

This document tracks the current Burley subsurface-scattering parity state between Unreal's
profile-driven pipeline and Filament's newer dedicated SSS payload path. The high-level rendering
order is now much closer than before, but important differences remain in data modeling, Burley
scaling math, transmission, and profile handling.

## Pipeline Outline

### Unreal Burley / profile pipeline

1. Base pass writes profile-related shading state into the GBuffer / Substrate payload.
   - Base color is intentionally handled for later SSS recombine, rather than being baked the same
     way as ordinary lit output.
   - Reference:
     [BasePassPixelShader.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/BasePassPixelShader.usf#L1238)
2. Postprocess derives Burley parameters from profile data.
   - `SurfaceAlbedo`
   - `DiffuseMeanFreePath`
   - `WorldUnitScale`
   - `SurfaceOpacity`
   - Reference:
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L34)
3. Blur pass filters only the subsurface diffuse lighting signal.
   - Kernel comes from profile / Burley math, not from a hand-authored terminator band.
   - Reference:
     [SeparableSSS.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SeparableSSS.ush#L224)
4. Recombine reconstructs diffuse/specular, blends unfiltered diffuse vs filtered SSS lighting
   using profile tint / weight, reapplies stored base color, then adds preserved specular.
   - Reference:
     [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1052)

### Current Filament pipeline

1. Color pass writes a dedicated SSS payload.
   - `sssDiffuse`, `sssNormal`, `sssParams`, `sssProfile`, `sssExtra`, `sssTransmission`
   - References:
     [RendererUtils.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/RendererUtils.cpp#L126)
     [RendererUtils.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/RendererUtils.cpp#L320)
2. Material/export stage packs Burley-ish per-pixel data into those MRTs.
   - Reference:
     [surface_main.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_main.fs#L77)
3. Postprocess runs a 2-pass blur over the SSS payload.
   - Reference:
     [PostProcessManager.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/PostProcessManager.cpp#L1261)
4. Recombine divides out surface albedo, blends lighting-space diffuse, reapplies albedo, then
   adds preserved specular and separate transmission.
   - Reference:
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L304)

The architectural order is now reasonably aligned: separate SSS payload, blur diffuse lighting,
preserve specular, recombine in lighting space, and apply transmission separately.

## Data Flow

### Unreal data flow

- Profile asset / profile texture provides:
  - surface albedo
  - diffuse mean free path
  - world unit scale
  - tint
  - boundary color bleed
  - transmission params
  - dual spec params
- Postprocess consumes profile id or per-pixel Burley payload and fetches the correct profile
  entries.
- References:
  [SubsurfaceProfileCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SubsurfaceProfileCommon.ush#L82)
  [TransmissionCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/TransmissionCommon.ush#L12)

### Filament data flow

- Material inputs carry:
  - `subsurfaceColor`
  - `scatteringDistance`
  - `worldUnitScale`
  - `boundaryColorBleed`
  - `extinctionScale`
  - `normalScale`
  - `scatteringDistribution`
  - `transmissionTintColor`
  - Reference:
    [surface_material_inputs.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_material_inputs.fs#L44)
- These are copied into pixel params.
  - Reference:
    [surface_shading_lit.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_shading_lit.fs#L228)
- They are exported into SSS MRTs:
  - `sssParams`: `baseColor + scatteringDistance`
  - `sssProfile`: `subsurfaceColor + packed(normalScale, scatteringDistribution)`
  - `sssExtra`: `boundaryColorBleed + worldUnitScale`
  - `sssTransmission`: `transmissionTintColor + extinctionScale`
  - Reference:
    [surface_main.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_main.fs#L81)
- Blur / recombine consume those buffers here:
  - Reference:
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L179)

Filament is aligned in spirit, but it is still a direct per-pixel payload pipeline rather than
Unreal's profile-id / profile-table model.

## What Now Aligns

- Lighting-space recombine is conceptually aligned.
  - Unreal: `lerp(Diffuse, SSSColor, FadedTint)` then `* StoredBaseColor + Specular`
  - Filament: divide by `surfaceAlbedo`, blend blurred vs unblurred diffuse in lighting space,
    then reapply albedo and add preserved specular
  - References:
    [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1058)
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L307)
- World-scale-aware blur radius is structurally aligned.
  - Unreal uses `CalculateBurleyScale`
  - Filament uses `sd * worldUnitScale` plus projection scaling
  - References:
    [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L89)
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L215)
- Burley kernel shape is directionally aligned.
  - Unreal uses Burley/common scaling and LUT or per-pixel generation
  - Filament uses per-channel `burleyProfile3`
  - References:
    [BurleyNormalizedSSSCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/BurleyNormalizedSSSCommon.ush#L7)
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L123)

## Detailed Gap List

1. `SurfaceAlbedo` is not used the same way Unreal uses it for Burley scaling.
   - Unreal uses `SurfaceAlbedo` in Burley scaling-factor math via `GetScalingFactor` /
     `GetScalingFactor3D`.
   - Filament currently uses `surfaceAlbedo` mainly for divide/reapply in recombine, not for kernel
     scaling itself.
   - References:
     [BurleyNormalizedSSSCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/BurleyNormalizedSSSCommon.ush#L41)
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L108)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L209)

2. Mean free path is still treated more simply than Unreal's DMFP / profile model.
   - Unreal distinguishes decoded diffuse mean free path, profile sampling component, scaling
     factors, and profile radius scale.
   - Filament uses `scatteringDistance * worldUnitScale * centerTint` directly for channel spread.
   - Reference:
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L215)

3. There is no profile-id-based lookup or separation.
   - Unreal fetches profile rows and keeps profile identity through postprocess.
   - Filament has no profile id buffer; it only carries per-pixel values.
   - Mixed materials can therefore only be separated heuristically via local payload similarity.
   - References:
     [SeparableSSS.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SeparableSSS.ush#L158)
     [SubsurfaceProfileCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SubsurfaceProfileCommon.ush#L62)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L254)

4. Boundary color bleed logic is not aligned.
   - Unreal stores boundary bleed per profile and consumes it using profile identity.
   - Filament uses a local color-distance test between sampled `subsurfaceColor` values and mixes
     with `boundaryColorBleed`.
   - This is an approximation, not the same consumption model.
   - Reference:
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L255)

5. Kernel generation is still not Unreal's Burley implementation.
   - Unreal uses profile LUTs or on-the-fly Burley kernel generation with the proper scaling-factor
     pipeline.
   - Filament uses a simpler separable per-channel profile function.
   - Structurally similar, but not yet mathematical parity.
   - References:
     [SeparableSSS.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SeparableSSS.ush#L176)
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L126)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L123)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L239)

6. Recombine weight is still not Unreal's `Tint * LerpFactor`.
   - Unreal's `FadedTint` is profile tint multiplied by recombine / upsample factors.
   - Filament currently uses `centerTint * (scatterAmount * energyWeight)`.
   - This is closer than a terminator-band heuristic, but it is still a custom energy heuristic.
   - References:
     [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1056)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L311)

7. Diffuse / specular separation is still only approximate.
   - Unreal has explicit lighting reconstruction and extracted non-subsurface specular.
   - Filament computes `centerSpecular = centerColor - centerSetupDiffuse`.
   - That is structurally correct, but it still depends on the fidelity of `centerSetupDiffuse`.
   - References:
     [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1052)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L199)

8. Transmission remains much simpler than Unreal's transmission profile pipeline.
   - Unreal includes:
     - `ExtinctionScale`
     - `NormalScale`
     - `ScatteringDistribution`
     - `OneOverIOR`
     - transmission profile lookup by thickness
   - Filament currently includes:
     - tint
     - extinction scale
     - normal scale
     - scattering distribution
     - no IOR usage
     - no transmission profile texture lookup by thickness
   - References:
     [TransmissionCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/TransmissionCommon.ush#L12)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L347)

9. `IOR` is authored in the sample but unused in the current pipeline.
   - Unreal transmission consumes `OneOverIOR`.
   - Filament's sample exposes `IOR`, but the engine payload and recombine path do not use it.
   - Reference:
     [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L1013)

10. Dual specular is still completely missing.
   - Unreal profile data includes `roughness0`, `roughness1`, and `lobeMix`.
   - Filament preserves ordinary specular, but does not implement dual-spec recombine.
   - References:
     [SubsurfaceProfileCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SubsurfaceProfileCommon.ush#L82)

11. There is no half-res / AFIS / quality-mode structure.
   - Unreal contains half-res recombine and tile / quality paths.
   - Filament currently has a full-resolution 2-pass blur and recombine only.
   - References:
     [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L56)
     [PostProcessManager.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/PostProcessManager.cpp#L1377)

12. There is no center-sample / CDF correction like Unreal's Burley path.
   - Unreal explicitly computes center-sample radius / CDF-related weighting.
   - Filament does not currently reproduce that part of the model.
   - References:
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L106)
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L126)

13. Filament debug views are useful, but they are no longer 1:1 with Unreal's internal stages.
   - `LIGHTING_DELTA` and `RECOMBINE_WEIGHT` are better aligned with the current Filament path.
   - Unreal's internal debugging still revolves around profile fetch, tile / quality logic, and
     reconstruction details that Filament does not mirror yet.
   - References:
     [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L776)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L337)

14. Sample / report artifacts are partially stale relative to the new pipeline.
   - Current code exposes newer debug concepts, but the existing capture folders still contain older
     names such as `terminator_window.png` and `band_mask.png`.
   - This is a workflow / documentation problem rather than a shader problem.
   - Reference:
     [captures/sss_burley/unreal_burley_reference](/Users/aspirin2ds/Workspace/github/filament/captures/sss_burley/unreal_burley_reference)

## Bottom Line

Current Filament is now aligned with Unreal at the architectural level:

- separate SSS payload
- blur diffuse while preserving specular
- recombine in lighting space
- reapply base color later
- keep transmission separate

The biggest remaining gaps are:

1. True profile-id / profile-table-driven data flow
2. Burley scaling-factor math using `SurfaceAlbedo` the way Unreal does
3. Proper transmission profile lookup, including IOR
4. Dual specular
5. Unreal quality-path details such as center-sample correction and half-resolution modes

## Suggested Next Work

1. Decide whether profile identity should become first-class in the payload before further tuning
   heuristics such as boundary bleed.
2. Bring kernel scaling closer to Unreal's Burley math, especially around `SurfaceAlbedo` and mean
   free path handling.
3. Either wire `IOR` through the actual transmission path or remove it from the sample until it is
   supported.
4. Refresh capture artifact naming and report language so the workflow matches the current debug
   outputs.
