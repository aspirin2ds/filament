# Burley SSS Parity Report

This report tracks the current Burley subsurface-scattering gap between Unreal's profile-driven
pipeline and the live Filament branch in this repository. The branch has moved beyond the original
single shared blur, but it is still materially simpler than Unreal: it carries only a small
dedicated SSS payload, uses a separable Burley-style blur with albedo-aware scaling, and still
applies heuristic recombine / transmission weighting on top of that.

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
- view-level `worldUnitScale` and `ior`
- unity view-level distance/color multipliers in the sample flow, so authored material values stay
  closer to Unreal's profile-style inputs

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
- still derives the scalar `LerpFactor` from local runtime heuristics rather than full
  Burley/profile terms
- adds a separate thin-region transmission term
- scales the blur calibration with view-level `worldUnitScale`
- scales the transmission term using view-level `ior`

References:

- [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L232)
- [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L314)

## Unreal Pipeline Reminder

Unreal's Burley / profile path still differs in a few foundational ways:

1. Base pass stores profile-related state for later profile-driven postprocess.
2. Postprocess derives Burley parameters such as `SurfaceAlbedo`, `DiffuseMeanFreePath`,
   `WorldUnitScale`, and `SurfaceOpacity`.
3. Blur filters diffuse-only SSS lighting using the full Burley/profile model.
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
- The sample flow now treats Burley inputs more like authored profile fields.
  - `Mean Free Path Color` and `Mean Free Path Distance` are authored on the material path.
  - The sample no longer reuses those same values as a second set of view-level multipliers.
  - This keeps the branch closer to Unreal's authored-profile mental model.
- Burley scaling is now more aligned with Unreal than before.
  - Filament stores `SurfaceAlbedo` separately and uses it during blur/recombine.
  - The blur now derives a per-channel distance from albedo, mean-free-path color,
    mean-free-path distance, and `worldUnitScale`.
  - This is still an approximation of Unreal's DMFP / profile model, but it is much closer to the
    current target than the older single-radius path.
  - References:
    [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L170)
    [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L34)
    [BurleyNormalizedSSSCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/BurleyNormalizedSSSCommon.ush#L125)
- The sample UI is now intentionally smaller.
  - Marble is the default preset.
  - `Mean Free Path Distance` defaults to `0.5`.
  - `IOR` defaults to `1.0` in the Marble preset and is exposed directly in the SSS panel.
  - `World Unit Scale` is exposed directly in the SSS panel.
  - `Mean Free Path Color` is exposed directly in the SSS panel.
  - References:
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L174)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L219)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L962)

## Biggest Remaining Gaps

1. No richer mean-free-path / Burley profile payload.
   - Unreal derives Burley scale from profile data including `SurfaceAlbedo`,
     `DiffuseMeanFreePath`, and `WorldUnitScale`.
   - Current Filament now has runtime `worldUnitScale` plus stored albedo, but it still does not
     carry an explicit Unreal-style Burley parameter block or decoded DMFP/profile payload.
   - The blur radius and energy weighting are therefore closer, but still less profile-faithful.
   - References:
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L89)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L170)

2. No profile identity.
   - Unreal can keep profile identity through postprocess and fetch profile rows.
   - Current Filament has no profile id in the payload, so mixed-material separation can only be
     approximated indirectly.
   - Reference:
     [SeparableSSS.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SeparableSSS.ush#L158)

3. Recombine weight is still heuristic rather than profile-driven.
   - Current Filament now uses a more Unreal-like `FadedTint` blend shape, which is a meaningful
     structural alignment with Unreal's `Tint * LerpFactor`.
   - However, the scalar weight still comes from `terminatorAnchor`, `shadowSupport`,
     `backscatterSupport`, and similar local heuristics instead of Burley center-sample / profile
     terms such as those used around `CalculateCenterSampleWeight`.
   - Reference:
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L264)
     [PostProcessSubsurface.usf](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurface.usf#L1047)

4. Transmission remains approximate.
   - `ior` now contributes, which is an improvement.
   - However, there is still no richer transmission profile data, thickness-profile lookup, or
     per-pixel transmission payload beyond thickness from `sssNormal.a`.
   - References:
     [TransmissionCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/TransmissionCommon.ush#L12)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L314)

5. Dual specular is still missing.
   - Unreal profile data includes dual-spec terms.
   - Current Filament preserves ordinary specular only.
   - Reference:
     [SubsurfaceProfileCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SubsurfaceProfileCommon.ush#L82)

6. The sample/report workflow still uses older debug naming.
   - The current debug views are still `TERMINATOR_WINDOW`, `BAND_MASK`, and `TRANSMISSION`.
   - The report should describe those as the branch's current debug views, not as parity with
     newer conceptual stages that do not exist in code yet.
   - References:
     [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L774)
     [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L247)

7. Several Unreal profile UI fields are still effectively fixed white in this branch's sample flow.
   - `Tint`, `Boundary Color Bleed`, and `Transmission Tint Color` are currently white in the
     sample presets and are not meaningful active parity levers here.
   - That means the practical live tuning set is closer to:
     `Surface Albedo`, `Mean Free Path Color`, `Mean Free Path Distance`, `World Unit Scale`, and `IOR`.

## Recommended Next Step

The next highest-value step to shrink the gap is:

1. Tighten the scalar `LerpFactor` and center-sample behavior against Unreal's Burley path.
2. Replace the remaining local support heuristics with logic closer to Unreal's Burley/profile
   weighting:
   - use the new Burley-scale inputs more directly when deriving the scalar blend weight
   - reduce reliance on local terminator/support heuristics
   - move closer to Unreal's center-sample / CDF-driven behavior where practical

This is the best next move because it unlocks multiple parity improvements at once:

- it builds on the new albedo-aware and world-scale-aware Burley mapping already in the branch
- it targets the most obvious remaining single-material mismatch
- it reduces the most prominent remaining heuristic in recombine
- it gives a better base for judging whether profile ids or richer transmission payload are really
  needed next

## Why This Before Profile IDs

Profile IDs are important, but they are not the first blocker on this branch.

Right now, even a single-material scene still differs from Unreal because recombine is not truly
profile-weight-aware. Adding profile identity before tightening that would increase payload
complexity without addressing the more visible single-material mismatch first.

## Concrete Follow-Up Order

1. Build on the new albedo-aware and world-scale-aware Burley inputs already in the branch.
2. Add or derive richer per-pixel Burley scale inputs beyond tint + distance.
3. Tighten the scalar `LerpFactor` and center-sample behavior against Unreal's Burley math.
4. Re-evaluate whether the remaining error is mostly from profile weighting math or from mixed-profile
   separation.
5. If mixed-profile separation is then visibly wrong, add profile identity or a profile-compatible
   mask next.
6. After that, revisit richer transmission payload and dual specular.
