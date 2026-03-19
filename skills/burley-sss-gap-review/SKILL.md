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
   - reads depth
   - computes a scalar Burley kernel from view-global parameters
   - recombines blurred diffuse with preserved specular
7. `sample_sss_burley.cpp` drives comparison presets, debug views, metadata dumps, and capture reports

## Current Debug Harness

The current skill should assume these debug outputs exist today:

- `final`
- `diffuse_only`
- `specular_only`
- `sss_influence`
- `sss_membership`
- `depth`
- `normal`
- `pre_blur_diffuse`
- `post_blur_diffuse`

At the API level, `SubsurfaceScatteringDebugMode` currently includes:

- `NONE`
- `MEMBERSHIP`
- `INFLUENCE`
- `PRE_BLUR_DIFFUSE`
- `POST_BLUR_DIFFUSE`
- `DEPTH`
- `NORMAL`
- `BAND_MASK`

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

## Current Known Gaps

Use this as the default starting checklist, but re-verify each item against source before editing.

### A. Diffuse/specular separation is implemented, but profile payload is still incomplete

Current Filament state:

- auxiliary diffuse MRT exists
- specular is preserved during recombine
- setup buffer still carries only:
  diffuse RGB + membership alpha

Real remaining gap:

- no full per-pixel Burley profile payload yet
- blur still depends on view-global `scatteringDistance` and `subsurfaceColor`

Primary bucket:

- setup / decomposition
- parameterization

Likely patch area:

- `shaders/src/surface_main.fs`
- `filament/src/RendererUtils.cpp`
- `filament/src/PostProcessManager.cpp`
- `filament/src/materials/sss/sssBlur.mat`

### B. Per-pixel scattering parameters are still missing

Unreal resolves profile-driven radius and tint per pixel.

Current Filament state:

- `View::SubsurfaceScatteringOptions` provides global `scatteringDistance`
- `View::SubsurfaceScatteringOptions` provides global `subsurfaceColor`
- all SSS pixels share those blur parameters

Primary bucket:

- parameterization

### C. World-unit scaling is only tracked in the sample

Current Filament state:

- sample metadata stores `worldUnitScale`
- the engine path does not serialize or consume it per pixel
- blur radius uses projected scale plus global scattering distance only

Primary bucket:

- parameterization
- blur kernel / sampling

### D. Multi-profile / profile-id handling is missing

Current Filament state:

- membership exists
- profile identity does not
- different Burley materials would share one global blur profile

Primary bucket:

- reconstruction / context
- missing feature

### E. Boundary color bleed is not implemented

Current Filament state:

- tracked in sample metadata only
- not used in blur weighting or recombine

Primary bucket:

- missing feature

### F. Normals come from a stored SSS normal buffer

Current Filament state:

- blur uses the stored shaded normal buffer
- weighting and recombine now follow the same normal-map detail as the material shading path
- validation should focus on thin detail and grazing-angle behavior

Primary bucket:

- reconstruction / context

### G. Transmission parity is missing

Current Filament state:

- material thickness exists
- Burley transmission parity does not
- current work is focused on the lateral screen-space surface band

Primary bucket:

- missing feature

### H. Dual specular parity is missing

Current Filament state:

- specular preservation exists
- Unreal-style dual-spec profile controls do not

Primary bucket:

- missing feature

Do not collapse this into blur fixes unless the task explicitly asks for it.

### I. Half-res / quality variants are missing

Current Filament state:

- only the full-resolution separable path is present

Primary bucket:

- missing feature

### J. TAA-aware Burley quality parity is deferred

Current Filament state:

- the blur is applied before TAA
- baseline comparison workflow disables TAA for deterministic captures

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
