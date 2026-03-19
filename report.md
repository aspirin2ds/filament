# Burley SSS Parity Report

This report tracks the current Burley subsurface-scattering gap between Unreal's Burley pipeline
and the live Filament branch in this repository. The branch has moved beyond the original
single shared blur, but it is still materially simpler than Unreal: it carries only a small
dedicated SSS payload, uses a separable Burley-style blur with albedo-aware scaling, and still
relies on approximate Burley weighting in recombine and transmission.

## Current Filament Pipeline

### Color pass payload

The current branch now writes four dedicated SSS buffers during the color pass:

- `sssDiffuse`: diffuse lighting RGB plus SSS membership alpha
- `sssNormal`: shading normal XYZ plus thickness alpha
- `sssParams`: subsurface tint RGB plus scattering distance alpha
- `sssAlbedo`: stored surface albedo for lighting-space recombine

References:

- [surface_main.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_main.fs#L68)
- [RendererUtils.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/RendererUtils.cpp#L123)

### Blur path

Postprocess performs a 2-pass separable blur over the diffuse SSS buffer using:

- depth rejection
- normal rejection
- per-pixel mean-free-path color approximation from `sssParams.rgb`
- per-pixel mean-free-path distance from `sssParams.a`
- stored surface albedo from `sssAlbedo`
- view-level `worldUnitScale`, `ior`, `extinctionScale`, `normalScale`,
  `scatteringDistribution`, and `transmissionTintColor`
- unity view-level distance/color multipliers in the sample flow, so authored material values stay
  closer to Unreal's Burley-style inputs

References:

- [PostProcessManager.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/PostProcessManager.cpp#L1223)
- [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L1)
- [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L794)

### Recombine path

The recombine pass currently:

- reconstructs a specular estimate with `centerColor - centerSetupDiffuse`
- derives diffuse lighting by dividing setup diffuse by stored albedo
- blurs in lighting space and reapplies albedo at the end
- blends blurred vs unblurred lighting with a `FadedTint`-style weight
- now includes a Burley-style center-sample / CDF correction in the scalar blend weight
- still does not match Unreal's full Burley weighting model
- adds a separate thin-region transmission term
- scales the blur calibration with view-level `worldUnitScale`
- scales and shapes transmission using view-level `ior`, `extinctionScale`, `normalScale`,
  `scatteringDistribution`, and `transmissionTintColor`
- preserves direct / IBL specular from the real Burley shading path, which now includes a
  material-authored dual-specular lobe model

References:

- [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L232)
- [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L314)

## Unreal Pipeline Reminder

Unreal's Burley path still differs in a few foundational ways:

1. Base pass stores Burley-related state for later postprocess.
2. Postprocess derives Burley parameters such as `SurfaceAlbedo`, `DiffuseMeanFreePath`,
   `WorldUnitScale`, and `SurfaceOpacity`.
3. Blur filters diffuse-only SSS lighting using the full Burley model.
4. Recombine blends in lighting space, reapplies stored base color, and preserves more explicit
   diffuse/specular separation.

References:

- [BasePassPixelShader.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/BasePassPixelShader.usf#L1238)
- [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L34)
- [SeparableSSS.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SeparableSSS.ush#L224)
- [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1052)

## What Improved Recently

- `ior` is no longer dead sample state.
  - It is now part of `SubsurfaceScatteringOptions`.
  - It is clamped in `FView`.
  - It is forwarded through `PostProcessManager`.
  - It modulates the transmission approximation in `sssBlur.mat`.
  - References:
    [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L819)
    [View.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/details/View.cpp#L1454)
    [PostProcessManager.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/PostProcessManager.cpp#L1238)
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L321)
- Transmission controls are materially richer than before.
  - `extinctionScale`, `normalScale`, `scatteringDistribution`, and `transmissionTintColor` are
    now part of `SubsurfaceScatteringOptions`.
  - They are clamped in `FView`, forwarded through `PostProcessManager`, and consumed in
    `sssBlur.mat`.
  - `Transmission View` is now debug-exposed with a readable tonemapped preview.
  - References:
    [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L831)
    [View.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/details/View.cpp#L1454)
    [PostProcessManager.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/PostProcessManager.cpp#L1240)
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L397)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L978)
- `worldUnitScale` is now live at runtime.
  - It is part of `SubsurfaceScatteringOptions`.
  - It is clamped in `FView`.
  - It is forwarded through `PostProcessManager`.
  - It scales blur radius and related thresholds in `sssBlur.mat`.
  - It is exposed in the sample debug UI.
  - References:
    [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L823)
    [View.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/details/View.cpp#L1454)
    [PostProcessManager.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/PostProcessManager.cpp#L1240)
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L199)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L968)
- The sample flow now treats Burley inputs more like authored Burley material fields.
  - `Mean Free Path Color` and `Mean Free Path Distance` are authored on the material path.
  - The sample no longer reuses those same values as a second set of view-level multipliers.
  - This keeps the branch closer to Unreal's authored Burley mental model.
- Burley scaling is now more aligned with Unreal than before.
  - Filament stores `SurfaceAlbedo` separately and uses it during blur/recombine.
  - The blur now derives a per-channel distance from albedo, mean-free-path color,
    mean-free-path distance, and `worldUnitScale`.
  - This is still an approximation of Unreal's DMFP model, but it is much closer to the
    current target than the older single-radius path.
  - References:
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L170)
    [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L34)
    [BurleyNormalizedSSSCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/BurleyNormalizedSSSCommon.ush#L125)
- Center-sample / CDF-style weighting materially improved the visual match.
  - Filament now uses a Burley-style center-sample correction when deriving the scalar blend
    weight for `FadedTint`.
  - This reduced the previous reliance on pure terminator-style heuristics and produced a visibly
    better response in the current sample.
  - References:
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L292)
    [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L106)
    [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1047)
- The sample UI is now intentionally smaller.
  - Marble is the default preset.
  - `Mean Free Path Distance` defaults to `0.5`.
  - `IOR` defaults to `1.0` in the Marble preset and is exposed directly in the SSS panel.
  - `World Unit Scale` is exposed directly in the SSS panel.
  - `Mean Free Path Color` is exposed directly in the SSS panel.
  - `Transmission` and `Dual Specular` each have their own focused sections.
  - The unused sample-side `Emissive` controls were removed.
  - References:
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L174)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L219)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L962)
- Dual specular is no longer sample-only dead state.
  - `roughness0`, `roughness1`, and `lobeMix` are now authored on the Burley material.
  - The Burley direct-light shader uses a dual-lobe specular evaluation.
  - The Burley IBL path also uses the dual-lobe model instead of a postprocess approximation.
  - References:
    [sandboxSubsurfaceBurley.mat](/Users/aspirin2ds/Workspace/github/filament/samples/materials/sandboxSubsurfaceBurley.mat#L34)
    [surface_material_inputs.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_material_inputs.fs#L44)
    [surface_shading_model_subsurface_burley.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_shading_model_subsurface_burley.fs#L24)
    [surface_light_indirect.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_light_indirect.fs#L787)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L988)

## Biggest Remaining Gaps

1. No richer mean-free-path / Burley parameter payload.
   - Unreal derives Burley scale from data including `SurfaceAlbedo`,
     `DiffuseMeanFreePath`, and `WorldUnitScale`.
   - Current Filament now has runtime `worldUnitScale` plus stored albedo, but it still does not
     carry an explicit Unreal-style Burley parameter block or decoded DMFP payload.
   - The blur radius and energy weighting are therefore closer, but still less faithful.
   - References:
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L89)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L170)

2. Recombine weight is still approximate rather than Burley-driven.
   - Current Filament now uses a more Unreal-like `FadedTint` blend shape plus a center-sample /
     CDF-style correction, which is a meaningful structural alignment with Unreal's Burley path.
   - However, the scalar weight still is not derived from a full Unreal-style Burley data
     model and still retains a heuristic floor for stability.
   - Reference:
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L264)
     [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1047)

3. Transmission remains approximate even though the controls are richer.
   - `ior`, `extinctionScale`, `normalScale`, `scatteringDistribution`, and
     `transmissionTintColor` now all contribute to the live transmission term.
   - However, there is still no richer transmission parameter data, thickness-profile lookup, or
     per-pixel transmission payload beyond thickness from `sssNormal.a`.
   - References:
     [TransmissionCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/TransmissionCommon.ush#L12)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L397)

4. Dual specular is implemented, but it is still not full Unreal parity.
   - Unreal includes dual-spec terms alongside its broader Burley data model.
   - Current Filament now evaluates a real dual-lobe Burley specular in the shading path, but it
     is still authored directly on the material and is not yet coupled to a fuller Unreal-style
     parameter package.
   - References:
     [SubsurfaceProfileCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SubsurfaceProfileCommon.ush#L82)
     [surface_shading_model_subsurface_burley.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_shading_model_subsurface_burley.fs#L24)
     [surface_light_indirect.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_light_indirect.fs#L787)

5. The sample/report workflow still uses branch-specific debug naming.
   - The current debug views are still `TERMINATOR_WINDOW`, `BAND_MASK`, and `TRANSMISSION_VIEW`.
   - The report should describe those as the branch's current debug views, not as parity with
     newer conceptual stages that do not exist in code yet.
   - References:
     [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L774)
     [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L247)

6. Several Unreal UI fields are still effectively fixed white in this branch's sample flow.
   - `Tint`, `Boundary Color Bleed`, and `Transmission Tint Color` are currently white in the
     sample presets and are not meaningful active parity levers here.
   - That means the practical live tuning set is closer to:
     `Surface Albedo`, `Mean Free Path Color`, `Mean Free Path Distance`, `World Unit Scale`,
     transmission controls, and dual-spec controls.

## Recommended Next Step

The next highest-value step to shrink the gap is:

1. Re-evaluate the remaining mismatch in fresh captures now that center-sample weighting is better.
2. Decide whether the next blocker is:
   - richer Burley parameter fidelity
   - or transmission parity

This is the best next move because it unlocks multiple parity improvements at once:

- it uses the improved single-material baseline before adding more payload complexity
- it tells us whether the next change should target Burley parameter fidelity or
  transmission parity
- it gives a better base for judging whether richer transmission data or more Burley math is really
  needed next

## Why This Before More Data Plumbing

Right now, even a single-material scene still differs from Unreal because recombine, transmission,
and Burley scaling are not yet fully equivalent. The biggest remaining gaps are still visible
without any profile system, so the next decisions should come from fresh comparison captures and
direct Burley/transmission calibration rather than more identity plumbing.

## Concrete Follow-Up Order

1. Build on the new albedo-aware and world-scale-aware Burley inputs already in the branch.
2. Capture and review fresh Unreal-vs-Filament comparisons with the updated center-sample weighting.
3. Re-evaluate whether the remaining error is mostly from Burley weighting math or from
   transmission behavior.
4. Tighten Burley parameter fidelity further.
5. After that, revisit richer transmission data and calibration where the current direct-authored
   controls still diverge from Unreal.
