---
name: burley-sss-gap-review
description: Use when reviewing Filament's current Burley SSS implementation, comparing it against the local Unreal Engine reference, and turning real mismatches into an evidence-backed gap table, patch plan, or code change.
---

# Burley SSS Gap Review

Use this skill when the task is to understand Filament's current Burley normalized diffusion SSS path, compare it against Unreal's Burley / Subsurface Profile pipeline, and then produce one of:

1. a current-state implementation summary
2. a gap review with evidence
3. a prioritized patch plan
4. a focused fix for the highest-value mismatch

The job is to stay grounded in the current code. Do not repeat older assumptions if the implementation has already moved forward.

## What Is Implemented Right Now

Before calling something a gap, anchor yourself in the current Filament implementation:

- `SUBSURFACE_BURLEY` is a real material shading model.
- The color pass writes an auxiliary SSS MRT:
  `vec4(g_sssDiffuse, g_sssMask)`.
- The color pass also writes a stored shaded-normal SSS target for the blur path.
- The renderer allocates that auxiliary target only when SSS is enabled.
- Post-process applies a two-pass separable Burley blur.
- The blur is depth-aware and uses the stored shaded normal buffer.
- The final recombine preserves specular by reconstructing it from:
  `centerColor - centerSetupDiffuse`.
- The sample already exposes a deterministic debug / comparison harness.

Do not describe the implementation as "missing diffuse/specular separation" or "missing debug views". Those were older states and are no longer accurate.

## Inputs You Need

- Filament repo root: current workspace
- Unreal source tree: `../UnrealEngine`
- Filament skill target:
  `skills/burley-sss-gap-review/SKILL.md`
- Sample harness:
  `samples/sample_sss_burley.cpp`
- Post-process blur:
  `filament/src/materials/sss/sssBlur.mat`
- Post-process integration:
  `filament/src/PostProcessManager.cpp`
- Color-pass MRT allocation:
  `filament/src/RendererUtils.cpp`
- Shading path:
  `shaders/src/surface_shading_model_subsurface_burley.fs`
- Repo notes if present:
  `report.md`

## Main Filament Reference Files

Read these first:

- `filament/include/filament/Options.h`
- `filament/src/details/Renderer.cpp`
- `filament/src/RendererUtils.cpp`
- `filament/src/PostProcessManager.cpp`
- `filament/src/materials/sss/sssBlur.mat`
- `shaders/src/surface_main.fs`
- `shaders/src/surface_lighting.fs`
- `shaders/src/surface_light_indirect.fs`
- `shaders/src/surface_shading_lit.fs`
- `shaders/src/surface_shading_model_subsurface_burley.fs`
- `samples/sample_sss_burley.cpp`

## Main Unreal Reference Files

Start narrow and source-first:

- `../UnrealEngine/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h`
- `../UnrealEngine/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp`

Only expand beyond those if the current question requires it.

## Current Filament Pipeline Snapshot

Use this exact mental model when reviewing gaps:

1. Material shading marks Burley pixels and accumulates scatterable diffuse into:
   `g_sssDiffuse`, `g_sssMask`
2. `surface_main.fs` writes diffuse/mask plus a stored view-normal target
3. `RendererUtils.cpp` allocates the `RGBA16F` SSS diffuse and normal buffers when SSS is enabled
4. `Renderer.cpp` forces the main HDR color buffer to RGBA when Burley SSS is active
5. `PostProcessManager.cpp` runs horizontal then vertical blur
6. `sssBlur.mat`:
   - reads main color
   - reads SSS diffuse/setup buffer
   - reads stored SSS normals
   - reads per-pixel SSS params
   - reads depth
   - computes a Burley kernel from per-pixel radius plus view-global multipliers
   - recombines blurred diffuse with preserved specular and a thin-region transmission term
7. `sample_sss_burley.cpp` drives comparison presets, debug views, metadata dumps, and capture reports

## Current Debug Harness

The current skill should assume these debug outputs exist today:

- `final`
- `sss_influence`
- `sss_membership`
- `pre_blur_diffuse`
- `post_blur_diffuse`
- `terminator_window`
- `band_mask`
- `transmission`

At the API level, `SubsurfaceScatteringDebugMode` currently includes:

- `NONE`
- `MEMBERSHIP`
- `INFLUENCE`
- `PRE_BLUR_DIFFUSE`
- `POST_BLUR_DIFFUSE`
- `TERMINATOR_WINDOW`
- `BAND_MASK`
- `TRANSMISSION`

If a bug is hard to isolate, prefer extending this debug path before changing the main shading behavior.

## What The Sample Already Captures

`samples/sample_sss_burley.cpp` is more than a demo. It already contains:

- gap rows for the current comparison report
- deterministic viewpoint / artifact capture logic
- a metadata dump for Unreal-style Burley terms
- a markdown comparison report generator
- a preset that explicitly tracks Unreal-style profile concepts in sample-space metadata

When updating docs or plans, keep the skill aligned with that sample's current report language.

## Gap Categories

Classify each mismatch into exactly one primary bucket before patching:

- parameterization
  Unreal stores or resolves a control that Filament still keeps global, drops, or maps only approximately.
- setup / decomposition
  The setup buffer does not carry enough information for blur and recombine.
- reconstruction / context
  Depth, normals, profile id, thickness, or other local context is missing or reconstructed weakly.
- blur kernel / sampling
  Radius, kernel shape, sample strategy, or projected scaling differs materially.
- bilateral rejection
  Tap rejection is too weak, too strong, or missing profile awareness.
- recombine
  The blur result is added back at the wrong stage or with the wrong formula.
- missing feature
  Transmission, boundary bleed, dual specular, multi-profile handling, half-res variants, and similar parity gaps.

Do not patch before naming the bucket.

## Repeatable Review Workflow

### 1. Confirm the current Filament state first

Run source inspection before carrying forward an old assumption:

```bash
rg -n "SubsurfaceScatteringDebugMode|SubsurfaceScatteringOptions" \
  filament/include/filament/Options.h samples/sample_sss_burley.cpp

rg -n "g_sssDiffuse|g_sssMask|SUBSURFACE_BURLEY" \
  shaders/src/surface_main.fs \
  shaders/src/surface_lighting.fs \
  shaders/src/surface_light_indirect.fs \
  shaders/src/surface_shading_lit.fs \
  shaders/src/surface_shading_model_subsurface_burley.fs

rg -n "sssDiffuse|hasSubsurfaceScattering|RGBA16F" \
  filament/src/RendererUtils.cpp filament/src/details/Renderer.cpp

sed -n '1,320p' filament/src/materials/sss/sssBlur.mat
sed -n '1218,1355p' filament/src/PostProcessManager.cpp
```

Record what is already true before writing a gap list.

### 2. Read Unreal top-down

```bash
sed -n '25,140p' ../UnrealEngine/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h
sed -n '1,220p' ../UnrealEngine/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp
rg -n "Burley|SubsurfaceProfile|Mean Free Path|Boundary Color Bleed|Transmission|Dual Specular" \
  ../UnrealEngine/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h \
  ../UnrealEngine/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp
```

Extract:

- what Unreal stores per profile
- what Unreal resolves per pixel
- what belongs to setup, blur, recombine, transmission
- which quality branches matter for the current comparison

### 3. Build a gap table before patching

Use this exact row format:

```markdown
| Behavior | Unreal reference | Filament current | Symptom | Root cause | Patch location | Validation |
| --- | --- | --- | --- | --- | --- | --- |
```

Keep one root cause per row.

### 4. Patch from the lowest-level root cause upward

Preferred order:

1. setup buffer payload
2. per-pixel parameter availability
3. kernel scaling / sampling
4. bilateral rejection
5. recombine
6. debug / capture tooling
7. optional quality or performance variants

Avoid tweaking recombine to hide missing per-pixel setup data.

## Band Math Parity Summary (verified 2026-03-22)

The following core math is **identical** between Filament and Unreal and does not need patching:

- **Diffusion profile R(r)**: `s/(8π·r)·(exp(-s·r) + exp(-s·r/3))`, `s=1/d`
  - Filament: `sssBlur.mat:48-52` (`burleyProfile3`)
  - Unreal: `BurleyNormalizedSSSCommon.ush:10-16` (`Burley_Profile`)
- **Scaling factors S(A)**: SearchLight `3.5+100·(A-0.33)⁴`, Perpendicular `1.85-A+7·|A-0.8|³`
  - Filament: `sssBlur.mat:82-95`
  - Unreal: `BurleyNormalizedSSSCommon.ush:56-95`
- **DMFP→MFP conversion**: `0.6 · Perp(A)/Search(A) · dmfp`
  - Filament: `sssBlur.mat:92-95` (`getMfpFromDmfpApprox`)
  - Unreal: `BurleyNormalizedSSSCommon.ush:105-113` (`GetMFPFromDMFPCoeff`)
- **CDF / CDF⁻¹ / PDF**: `CDF(r) = 1-0.25·exp(-r/d)-0.75·exp(-r/(3d))`, analytic inverse via `G=1+4U(2U+√(1+4U²))`
  - Filament: `sssBlur.mat:56-80`
  - Unreal: `BurleyNormalizedSSSCommon.ush:154-197`
- **Normal bilateral weight**: `sqrt(saturate(dot(N_c, N_s)·0.5+0.5))`
  - Filament: `sssBlur.mat:125-127`
  - Unreal: `SubsurfaceBurleyNormalized.ush:935`
- **Per-channel RGB weighting**: both evaluate profile with per-channel D (vec3) and divide by scalar PDF from dominant channel
  - Filament: `sssBlur.mat:188-190` — `kernelWeight = burleyProfile3(r, centerD) / pdf(r, dominantD)`
  - Unreal: `SubsurfaceBurleyNormalized.ush:961-962` — `DiffusionProfile(L.xyz, S3D.xyz, r) / Pdf`

## Current Known Gaps

Use this as the default starting checklist, but re-verify each item against source before editing.

### Gap Table

| Behavior | Unreal reference | Filament current | Symptom | Root cause | Patch location | Bucket |
|---|---|---|---|---|---|---|
| 2D disc vs separable blur | Monte Carlo disc: random `(radius, angle)` over 2D disc, 16-256 taps (`SubsurfaceBurleyNormalized.ush:862-971`) | Separable H+V: stratified 1D `xi=i/(N+1)`, symmetric ±offset (`sssBlur.mat:179-234`) | Possible axis-aligned scatter artifacts | Architectural choice — separable is 2× cheaper but approximate for non-Gaussian kernels | `sssBlur.mat` blur loop | blur kernel / sampling |
| Bilateral depth rejection | 3D Euclidean radius correction: `R3D = √(R2D² + Δdepth²)` → profile re-eval at R3D (`SubsurfaceBurleyNormalized.ush:944-945`) | Linear depth ramp: `saturate(1 - \|Δdepth\| / (supportRadius·3))` (`sssBlur.mat:205-206`) | Over-rejection on gentle curves, under-rejection on steep depth gaps | Missing 3D radius correction — linear heuristic instead of physics-based re-eval | `sssBlur.mat:195-206` | bilateral rejection |
| Center sample reweighting | CDF-split: center covers `[0, CDF(R_center)]`, scatter covers `[CDF(R_center), 1]`, lerp blend (`SubsurfaceBurleyNormalized.ush:838-860, 989-991`) | Center = weight `vec3(1.0)`, no CDF split (`sssBlur.mat:175-176`) | Slight center bias — scatter contribution underweighted relative to center | No CDF-based center reweighting | `sssBlur.mat:175-176` | blur kernel / sampling |
| Boundary color bleed | Per-profile `BoundaryColorBleed` tints cross-profile contributions (`SubsurfaceBurleyNormalized.ush:956, 987`) | `boundaryColorBleed` param exists in `.mat` but is **unused** in blur loop | Uncontrolled color bleeding across different SSS materials | Param wired but not consumed | `sssBlur.mat` blur loop | missing feature |
| Per-pixel profile ID | Profile texture lookup per pixel via `SubsurfaceProfileInt` (`SubsurfaceBurleyNormalized.ush:797-798`) | All SSS pixels share one global blur config from `SubsurfaceScatteringOptions` | Multi-material scenes get same scatter radius/tint | No profile-id MRT channel, no per-pixel DMFP/albedo lookup | `surface_main.fs`, `sssBlur.mat` | parameterization |
| Per-pixel scattering params | Per-profile `DiffuseMeanFreePath` (vec4 RGB+sampling), `SurfaceAlbedo` (vec4) from profile texture | Global `scatteringDistance` × per-pixel scalar, global `subsurfaceColor` × per-pixel tint | Different body parts cannot have different scatter radii | View-global params, no profile texture | `Options.h`, `PostProcessManager.cpp`, `sssBlur.mat` | parameterization |
| Energy normalization | `1/0.99995` correction factor + CDF-split center reweighting (`SubsurfaceBurleyNormalized.ush:974-984`) | `totalDiffuse / max(totalWeight, 1e-4)` — no energy correction factor | Minor energy loss (~0.005%) | Missing compensation constant | `sssBlur.mat:237` | blur kernel / sampling |
| Transmission model | Full Burley transmission: `0.25·A·(exp(-S·r) + 3·exp(-S·r/3))` with MFP scale factor 100× and 32-sample LUT (`BurleyNormalizedSSSCommon.ush:270-317`) | Thin-region transmission approximation in recombine | Less accurate back-scattering, especially for thicker geometry | Simplified transmission model | `sssBlur.mat` recombine | missing feature |
| Dual specular profile | Per-profile dual-spec controls stored in profile texture (`SubsurfaceProfileCommon.ush` at `SSSS_DUAL_SPECULAR_OFFSET`) | `burleyDualSpecularLobe` exists with hardcoded roughness 0.75/1.3 (`surface_brdf.fs:266-293`) | No artist control over specular lobe mix | Hardcoded dual-spec params, no profile texture | `surface_brdf.fs`, `surface_material_inputs.fs` | missing feature |
| Half-res / quality variants | Half-res option, quality-adaptive sample count, mip-level selection (`SubsurfaceBurleyNormalized.ush:917-922`) | Full-resolution only, user-configurable sample count | Higher cost on mobile, no automatic LOD | Missing half-res path and mip selection | `PostProcessManager.cpp` | missing feature |
| TAA-aware quality | Temporal variance tracking, history-based sample reuse (`SubsurfaceBurleyNormalized.ush:1006+`) | Blur before TAA, no variance tracking | No temporal amortization of scatter noise | Missing temporal feedback loop | `Renderer.cpp` scheduling | missing feature |

### A. Diffuse/specular separation is implemented, but profile payload is still incomplete

Current Filament state:

- auxiliary diffuse MRT exists (4 targets: diffuse+mask, normal+thickness, params, albedo)
- specular is preserved during recombine via `max(centerColor - centerSetupDiffuse, 0)`
- per-pixel params MRT carries `subsurfaceColor.rgb` + `scatteringDistance` scalar
- blur still multiplies per-pixel values by **view-global** `scatteringDistance` and `subsurfaceColor`

Real remaining gap:

- no profile-id or profile-texture lookup — all pixels share one global multiplier
- per-pixel `scatteringDistance` (scalar) × global scalar is less expressive than UE's per-profile vec4 DMFP

Primary bucket:

- setup / decomposition
- parameterization

Likely patch area:

- `shaders/src/surface_main.fs`
- `filament/src/RendererUtils.cpp`
- `filament/src/PostProcessManager.cpp`
- `filament/src/materials/sss/sssBlur.mat`

### B. Per-pixel scattering parameters are partially implemented

Unreal resolves profile-driven radius and tint per pixel from a profile texture indexed by profile ID.

Current Filament state:

- per-pixel: `subsurfaceColor` (vec3) and `scatteringDistance` (float) written to params MRT (`surface_main.fs` fragColor3)
- per-pixel: `surfaceAlbedo` (vec3) written to albedo MRT (`surface_main.fs` fragColor4)
- blur computes per-pixel `centerD = centerTint * scaledSd / SearchLightS(albedo)` — this is correct per-pixel math
- **but**: `scaledSd = centerParamsData.a * materialParams.scatteringDistance * materialParams.worldUnitScale` — the global multipliers mean all materials share the same base scale

Primary bucket:

- parameterization

### C. World-unit scaling is consumed in the blur path

Current Filament state (updated):

- `worldUnitScale` is a material parameter on `sssBlur.mat` (line 15)
- it is consumed in the blur: `scaledSd = sd * materialParams.worldUnitScale` (line 159)
- `PostProcessManager.cpp` sets it from `SubsurfaceScatteringOptions::worldUnitScale`
- this is a view-global value, not per-profile

Primary bucket:

- parameterization (view-global, not per-profile)

### D. Multi-profile / profile-id handling is missing

Current Filament state:

- membership mask exists (via `g_sssMask` alpha)
- profile identity does not — no profile-id channel in MRT
- different Burley materials share one global blur profile
- UE stores profile ID per pixel and looks up DMFP, albedo, boundary bleed per profile

Primary bucket:

- reconstruction / context
- missing feature

### E. Boundary color bleed param exists but is not consumed

Current Filament state:

- `boundaryColorBleed` declared as material parameter in `sssBlur.mat` (line 22)
- set from `SubsurfaceScatteringOptions::boundaryColorBleed` in `PostProcessManager.cpp`
- **not read or used anywhere in the blur loop** (`sssBlur.mat:129-256`)
- UE multiplies `BoundaryColorBleedAccum` into final result (`SubsurfaceBurleyNormalized.ush:987`)

Primary bucket:

- missing feature

### F. Bilateral depth uses linear ramp instead of 3D radius correction

Current Filament state:

- `depthWeightP = saturate(1.0 - deltaDepthP / depthFalloff)` where `depthFalloff = supportRadius * 3.0`
- this is a soft linear ramp that rejects hard edges
- UE instead computes `R3D = sqrt(R2D² + Δdepth²)` and re-evaluates the Burley profile at R3D
- UE's approach is more physically correct: the profile's own exponential decay handles curvature naturally

Validation focus: compare blur falloff near ears, nose bridge, and other high-curvature regions.

Primary bucket:

- bilateral rejection

### G. Center sample reweighting is missing

Current Filament state:

- center pixel enters the accumulator as `totalDiffuse = centerDiffuse`, `totalWeight = vec3(1.0)` — flat weight
- UE splits the CDF: center covers `[0, CDF(R_center)]`, scatter covers `[CDF(R_center), 1]`
- UE then lerps: `lerp(scattered, center, CenterSampleWeight)` for unbiased blending
- without this, Filament's blur is slightly biased toward the center pixel

Primary bucket:

- blur kernel / sampling

### H. Normals come from a stored SSS normal buffer — parity OK

Current Filament state:

- blur uses the stored shaded normal buffer (fragColor2)
- 5-tap `estimateSmoothedStoredNormal` averages macro normals to suppress high-frequency detail
- normal bilateral formula matches UE exactly
- validation should focus on thin detail and grazing-angle behavior

Primary bucket:

- reconstruction / context (low priority — functionally correct)

### I. Transmission parity is approximate

Current Filament state:

- material thickness exists
- a thin-region transmission term exists in recombine
- UE uses full Burley transmission: `0.25·A·(exp(-S·r) + 3·exp(-S·r/3))` with 32-sample LUT and `TransmissionMFPScaleFactor=100`
- Filament's approximation is simpler

Primary bucket:

- missing feature

### J. Dual specular parity — hardcoded, not profile-driven

Current Filament state:

- `burleyDualSpecularLobe` exists (`surface_brdf.fs:266-293`) with roughness 0.75/1.3
- these are hardcoded defaults, not exposed as per-profile controls
- UE stores dual-specular params in the profile texture

Primary bucket:

- missing feature

Do not collapse this into blur fixes unless the task explicitly asks for it.

### K. Half-res / quality variants are missing

Current Filament state:

- only the full-resolution separable path is present
- UE supports half-res with mip-level selection based on sample count and DMFP

Primary bucket:

- missing feature

### L. TAA-aware Burley quality parity is deferred

Current Filament state:

- the blur is applied before TAA
- baseline comparison workflow disables TAA for deterministic captures
- UE has temporal variance tracking and history-based quality adaptation

Primary bucket:

- missing feature
- validation policy

## Required Evidence Before Claiming A Gap

Every claimed gap should be backed by at least one of:

- a local Unreal source reference
- a Filament source reference
- a deterministic sample capture or report artifact
- a parameter mapping mismatch

Prefer source references first.

## Required Evidence Before Claiming A Fix

Before claiming a fix, show:

1. which root cause was patched
2. which files changed
3. which debug view proves it
4. which final-view symptom changed
5. what still differs from Unreal

Do not settle for "looks closer".

## Validation Loop

After each meaningful patch:

1. rebuild

```bash
cmake --build /Users/aspirin2ds/Workspace/github/filament/out/cmake-debug \
  --target filament sample_sss_burley -j 4
```

2. run the sample if the environment supports it
3. capture the deterministic comparison set
4. compare at least:
   - `sss_membership`
   - `sss_influence`
   - `pre_blur_diffuse`
   - `post_blur_diffuse`
   - `final`
5. update the gap table

If runtime execution is blocked, still record:

- build status
- why runtime verification was blocked
- which source references most directly support the conclusion

## Patch Triage Rules

- Patch now if it blocks the main reference comparison:
  setup payload, per-pixel radius/tint, recombine bugs, missing debug evidence.
- Patch soon if it materially affects band placement or edge stability:
  world-unit scale, profile id handling, better normal input.
- Defer if it is orthogonal to current diffuse-band parity:
  dual specular, half-res optimization, temporal tuning.
- Isolate as a separate task if it changes the transport family:
  transmission.

## Practical Review Notes

- Treat `samples/sample_sss_burley.cpp` as a source of truth for the current comparison workflow.
- Treat `sssBlur.mat` as the root of truth for current blur, rejection, debug views, and recombine behavior.
- Treat `RendererUtils.cpp` and `Renderer.cpp` as the root of truth for whether the pipeline really allocates and schedules the SSS path.
- When the sample's gap matrix and the skill disagree, update the skill to match the code first, then decide whether the sample text also needs updating.
