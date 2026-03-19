# Burley SSS Parity Report

This report tracks the current Burley subsurface-scattering gap between Unreal's profile-driven
pipeline and the live Filament branch in this repository. The branch has moved beyond the original
single shared blur, but it is still materially simpler than Unreal: it carries only a small
dedicated SSS payload, uses a single separable Burley-style blur, and applies heuristic recombine
and transmission weighting.

## Current Filament Pipeline

### Color pass payload

The current branch writes three dedicated SSS buffers during the color pass:

- `sssDiffuse`: diffuse lighting RGB plus SSS membership alpha
- `sssNormal`: shading normal XYZ plus thickness alpha
- `sssParams`: subsurface tint RGB plus scattering distance alpha

References:

- [surface_main.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_main.fs#L68)
- [RendererUtils.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/RendererUtils.cpp#L123)

### Blur path

Postprocess performs a 2-pass separable blur over the diffuse SSS buffer using:

- depth rejection
- normal rejection
- per-pixel tint from `sssParams.rgb`
- per-pixel scattering distance from `sssParams.a`
- view-level `scatteringDistance`, `subsurfaceColor`, and now `ior`

References:

- [PostProcessManager.cpp](/Users/aspirin2ds/Workspace/github/filament/filament/src/PostProcessManager.cpp#L1223)
- [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L1)
- [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L794)

### Recombine path

The recombine pass currently:

- reconstructs a specular estimate with `centerColor - centerSetupDiffuse`
- computes a heuristic tinted diffuse influence mask
- applies a warm-tint remap to the blurred diffuse
- adds a separate thin-region transmission term
- now scales that transmission term using view-level `ior`

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
- The sample UI is now intentionally smaller.
  - Marble is the default preset.
  - `Scattering Distance` defaults to `0.5`.
  - `IOR` defaults to `1.0` in the Marble preset and is exposed directly in the SSS panel.
  - References:
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L174)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L219)
    [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L962)

## Biggest Remaining Gaps

1. No stored `SurfaceAlbedo` for lighting-space recombine.
   - Unreal stores and reuses base color explicitly during recombine.
   - Current Filament does not carry surface albedo in the SSS payload.
   - That means the branch still tints blurred diffuse directly instead of dividing to lighting
     space, blending there, and reapplying albedo.
   - References:
     [surface_main.fs](/Users/aspirin2ds/Workspace/github/filament/shaders/src/surface_main.fs#L72)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L283)

2. No explicit `WorldUnitScale` or richer mean-free-path payload.
   - Unreal derives Burley scale from profile data including `SurfaceAlbedo`,
     `DiffuseMeanFreePath`, and `WorldUnitScale`.
   - Current Filament only carries tint and scattering distance in `sssParams`.
   - The blur radius is therefore simpler and less profile-faithful.
   - References:
     [PostProcessSubsurfaceCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush#L89)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L170)

3. No profile identity.
   - Unreal can keep profile identity through postprocess and fetch profile rows.
   - Current Filament has no profile id in the payload, so mixed-material separation can only be
     approximated indirectly.
   - Reference:
     [SeparableSSS.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SeparableSSS.ush#L158)

4. Recombine is still heuristic rather than profile-driven.
   - Current Filament uses `terminatorAnchor`, `shadowSupport`, `backscatterSupport`, and
     `warmTint` heuristics.
   - Unreal's recombine logic is more explicitly tied to profile tint and weighting terms.
   - Reference:
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L264)

5. Transmission remains approximate.
   - `ior` now contributes, which is an improvement.
   - However, there is still no richer transmission profile data, thickness-profile lookup, or
     per-pixel transmission payload beyond thickness from `sssNormal.a`.
   - References:
     [TransmissionCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/TransmissionCommon.ush#L12)
     [sssBlur.mat](/Users/aspirin2ds/Workspace/github/filament/filament/src/materials/sss/sssBlur.mat#L314)

6. Dual specular is still missing.
   - Unreal profile data includes dual-spec terms.
   - Current Filament preserves ordinary specular only.
   - Reference:
     [SubsurfaceProfileCommon.ush](/Users/aspirin2ds/Workspace/github/filament/../UnrealEngine/Engine/Shaders/Private/SubsurfaceProfileCommon.ush#L82)

7. The sample/report workflow still uses older debug naming.
   - The current debug views are still `TERMINATOR_WINDOW`, `BAND_MASK`, and `TRANSMISSION`.
   - The report should describe those as the branch's current debug views, not as parity with
     newer conceptual stages that do not exist in code yet.
   - References:
     [Options.h](/Users/aspirin2ds/Workspace/github/filament/filament/include/filament/Options.h#L774)
     [sample_sss_burley.cpp](/Users/aspirin2ds/Workspace/github/filament/samples/sample_sss_burley.cpp#L247)

## Recommended Next Step

The next highest-value step to shrink the gap is:

1. Add stored `SurfaceAlbedo` to the SSS payload.
2. Change recombine to operate in lighting space:
   - derive center diffuse lighting by dividing out albedo
   - blur and blend in lighting space
   - reapply albedo at the end
   - keep preserved specular separate

This is the best next move because it unlocks multiple parity improvements at once:

- it moves the branch toward Unreal's actual recombine model
- it reduces the current over-reliance on warm-tint heuristics
- it creates the foundation needed for more faithful Burley scaling later
- it gives a better base for judging whether profile ids or richer transmission payload are really
  needed next

## Why This Before Profile IDs

Profile IDs are important, but they are not the first blocker on this branch.

Right now, even a single-material scene still differs from Unreal because recombine is not truly
lighting-space / albedo-aware. Adding profile identity before fixing that would increase payload
complexity without addressing the more visible single-material mismatch.

## Concrete Follow-Up Order

1. Add one more SSS payload channel or repack the existing payload so surface albedo is available in
   recombine.
2. Replace the current warm-tint recombine with lighting-space divide / blur / reapply.
3. Re-evaluate whether the remaining error is mostly from profile scaling math or from mixed-profile
   separation.
4. If mixed-profile separation is then visibly wrong, add profile identity or a profile-compatible
   mask next.
5. After that, revisit richer transmission payload and dual specular.
