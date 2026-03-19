---
name: burley-sss-gap-review
description: Use when comparing Filament's Burley SSS implementation against the local Unreal Engine source to find gaps, list issues, plan fixes, and patch the highest-value mismatches with a repeatable evidence-driven workflow.
---

# Burley SSS Gap Review

Use this skill when the task is to compare Filament's current Burley SSS path with Unreal Engine's Burley / Subsurface Profile implementation, then turn the differences into a concrete patch plan or code changes.

The goal is not to "eyeball until it looks fine". The goal is to:

1. find the reference behavior in Unreal
2. find the corresponding Filament path
3. classify the gap
4. capture evidence
5. patch the narrowest root cause first
6. validate with deterministic debug outputs and captures

## Inputs You Need

- Filament repo root: current workspace
- Unreal source tree: `../UnrealEngine`
- Current Filament Burley sample:
  `samples/sample_sss_burley.cpp`
- Current Filament SSS blur pass:
  `filament/src/materials/sss/sssBlur.mat`
- Current post-process integration:
  `filament/src/PostProcessManager.cpp`
- Current repo-level comparison notes:
  `report.md`

## Main Unreal Reference Files

- `../UnrealEngine/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h`
- `../UnrealEngine/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp`

Use these first before chasing wider Unreal files.

## Main Filament Comparison Files

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

## Gap Categories

Classify every mismatch into exactly one primary bucket before patching:

- parameterization
  Unreal has a control or profile field that Filament does not store or interpret correctly.
- setup / decomposition
  Filament captures the wrong inputs for the blur or recombine stages.
- reconstruction / context
  depth, normal, profile id, thickness, or radius context is missing or unstable.
- blur kernel / sampling
  kernel shape, radius scaling, adaptive sampling, or half/full-res policy differs.
- bilateral rejection
  taps should be rejected or attenuated differently by depth, normal, or profile.
- recombine
  scattered lighting is added back at the wrong stage or with the wrong formula.
- missing feature
  transmission, boundary bleed, profile cache, dual specular, TAA interaction, etc.

Do not patch before naming the bucket. This prevents "fixes" that only hide the symptom.

## Repeatable Workflow

### 1. Lock the comparison harness first

Before touching shader behavior, make sure the Filament side can produce deterministic evidence.

Required outputs:

- `final`
- `diffuse_only`
- `specular_only`
- `sss_influence`
- `sss_membership`
- `depth`
- `normal`
- `pre_blur_diffuse`
- `post_blur_diffuse`

Filament already exposes this through:

- `samples/sample_sss_burley.cpp`
- `filament/include/filament/Options.h`
- `filament/src/materials/sss/sssBlur.mat`

If the bug cannot be isolated with those views, add the missing debug output before changing the main shading path.

### 2. Read Unreal top-down

Start with:

```bash
sed -n '25,140p' ../UnrealEngine/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h
sed -n '1,180p' ../UnrealEngine/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp
rg -n "Burley|SubsurfaceProfile|Mean Free Path|Boundary Color Bleed|Transmission|Dual Specular" \
  ../UnrealEngine/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h \
  ../UnrealEngine/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp
```

Extract and record:

- what data Unreal stores per profile
- what data Unreal resolves per pixel
- what passes Unreal uses
- what is setup vs blur vs recombine vs transmission
- what is full-quality vs fallback / separable / half-res

Do not infer profile behavior from the UI screenshot alone when the source already states it.

### 3. Read Filament in the same order

Use the same conceptual order:

1. exposed controls / options
2. setup buffer contents
3. blur inputs
4. kernel and bilateral policy
5. recombine
6. sample / debug workflow

Suggested commands:

```bash
rg -n "SubsurfaceScatteringDebugMode|SubsurfaceScatteringOptions" \
  filament/include/filament/Options.h samples/sample_sss_burley.cpp

rg -n "sssDiffuse|subsurfaceScatteringBlur|hasSubsurfaceScattering" \
  filament/src/details/Renderer.cpp filament/src/RendererUtils.cpp filament/src/PostProcessManager.cpp

rg -n "g_sssDiffuse|g_sssMask|scatteringDistance|subsurfaceColor|SUBSURFACE_BURLEY" \
  shaders/src/surface_main.fs \
  shaders/src/surface_lighting.fs \
  shaders/src/surface_light_indirect.fs \
  shaders/src/surface_shading_lit.fs \
  shaders/src/surface_shading_model_subsurface_burley.fs

sed -n '1,260p' filament/src/materials/sss/sssBlur.mat
```

### 4. Build a gap table before patching

For each gap row, fill in:

- behavior
- Unreal reference
- current Filament behavior
- visible symptom
- likely root cause
- patch location
- validation view
- risk if changed

Use this exact row format in markdown:

```markdown
| Behavior | Unreal reference | Filament current | Symptom | Root cause | Patch location | Validation |
| --- | --- | --- | --- | --- | --- | --- |
```

Keep one root cause per row. Split rows if needed.

### 5. Patch from lowest-level root cause upward

Preferred order:

1. setup buffer correctness
2. per-pixel data availability
3. kernel scaling / sampling
4. bilateral rejection
5. recombine
6. sample tooling / capture automation
7. optional performance variants

Avoid patching recombine first if setup data is wrong. That usually creates a softer wrong answer.

## Current Known Mismatch Checklist

Use this as the default starting list. Confirm each item against the current code before editing.

### A. Per-pixel Burley profile data is incomplete

Unreal stores Burley profile fields such as:

- `SurfaceAlbedo`
- `MeanFreePathColor`
- `MeanFreePathDistance`
- `WorldUnitScale`
- `Tint`
- `BoundaryColorBleed`
- `TransmissionTintColor`
- dual specular controls

Current Filament state:

- sample tracks several reference-only values in UI/metadata
- engine blur still uses view-global `scatteringDistance` and `subsurfaceColor`
- setup buffer currently stores diffuse RGB plus membership alpha only

Primary bucket:

- parameterization
- setup / decomposition

Likely patch area:

- `shaders/src/surface_main.fs`
- `filament/src/RendererUtils.cpp`
- `filament/src/PostProcessManager.cpp`
- `filament/src/materials/sss/sssBlur.mat`

### B. World-unit scaling is not in the engine path

Unreal exposes `WorldUnitScale` as part of the Burley profile.

Current Filament state:

- sample can track it
- blur shader does not receive it as per-pixel data

Primary bucket:

- parameterization
- blur kernel / sampling

### C. Profile-aware masking is missing

Unreal has profile texture / profile id handling and a Burley profile id cache path.

Current Filament state:

- membership exists
- profile identity does not

Primary bucket:

- reconstruction / context
- missing feature

### D. Boundary color bleed is missing

Unreal exposes `BoundaryColorBleed`.

Current Filament state:

- tracked in sample metadata only
- not used in blur weighting or recombine

Primary bucket:

- missing feature

### E. Normals are reconstructed from depth

Unreal uses stronger context than the current lightweight reconstruction fallback.

Current Filament state:

- blur estimates normals from depth in `sssBlur.mat`

Primary bucket:

- reconstruction / context

### F. Transmission parity is missing

Unreal treats transmission as a separate Burley feature family.

Current Filament state:

- thickness exists on the material
- no Burley transmission parity path yet

Primary bucket:

- missing feature

### G. Dual specular parity is missing

Unreal exposes:

- `Roughness0`
- `Roughness1`
- `LobeMix`

Current Filament state:

- specular is preserved from blur
- dual-spec model is not implemented

Primary bucket:

- missing feature

Do not mix this into blur fixes unless the user explicitly asks for dual-spec next.

## Patch Triage Rules

Use these rules to decide what to patch now versus later:

- Patch now if the issue breaks the main reference comparison:
  setup buffer content, global-vs-per-pixel radius, recombine bug, missing debug view.
- Patch soon if it materially affects skin-band placement or shape:
  world-unit scale, profile id handling, stored normals.
- Defer if it is orthogonal to the current band comparison:
  dual specular, half-res optimization, TAA tuning.
- Isolate as a separate task if it changes the light transport family:
  transmission.

## Required Evidence Before Claiming A Gap

Every claimed gap should be backed by at least one of:

- a local Unreal source reference
- a Filament source reference
- a deterministic debug capture from the sample
- a parameter mapping mismatch

Prefer source references first, screenshots second.

## Required Evidence Before Claiming A Fix

Before claiming a fix, show:

1. which root cause was patched
2. which files changed
3. which debug view proves the change
4. which final-view symptom changed
5. what remains different from Unreal

Never present "looks closer now" as the only verification.

## Validation Loop

After each meaningful patch:

1. rebuild:

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

If runtime execution is blocked, still update the gap table with:

- build status
- why runtime verification was blocked
- which debug path or source inspection most directly supports the change

## Patch Patterns

### Pattern 1: Setup buffer gap

Symptoms:

- influence view is wrong even before recombine
- all SSS materials look the same
- blur radius ignores per-material intent

Typical edits:

- extend MRT setup output
- add shader outputs in `surface_main.fs`
- carry fields from material shading into the setup texture

### Pattern 2: Kernel / radius gap

Symptoms:

- band width is uniformly too broad or too narrow
- scene scale changes break the match

Typical edits:

- pass world-unit scale or per-pixel radius metadata
- update blur radius math in `sssBlur.mat`

### Pattern 3: Recombine gap

Symptoms:

- band visible in influence view but not in final
- whole model washes out

Typical edits:

- preserve original diffuse/specular split
- add only scattered diffuse delta back

### Pattern 4: Context / edge gap

Symptoms:

- bleed across silhouette or across different materials
- wobbling edges on thin geometry

Typical edits:

- strengthen depth / normal / profile rejection
- move from reconstructed normals to stored normals

## Deliverables

When finishing a comparison or patching pass, produce:

- updated markdown gap table
- concise list of findings ordered by impact
- changed file list
- build result
- remaining high-priority gaps

If asked to only write the plan, do not patch code. Write the plan as a markdown artifact and keep the steps actionable.

## Good Output Shape

Use this section order:

1. reference points
2. current Filament mapping
3. confirmed gaps
4. recommended patch order
5. validation checklist
6. deferred items

## Stop Conditions

Stop and escalate to the user if:

- a proposed patch needs a new G-buffer or a major buffer format tradeoff
- runtime capture is impossible and the next patch would be speculative
- two candidate fixes have materially different API costs
- unrelated dirty worktree changes overlap the same files

Otherwise keep going until the gap is either patched, disproven, or explicitly deferred.
