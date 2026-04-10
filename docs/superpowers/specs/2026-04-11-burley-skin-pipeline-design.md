# Burley Skin Pipeline Design

Date: 2026-04-11

## Summary

Add a dedicated `skin` shading model to Filament, backed by a renderer-owned Burley-style
screen-space diffusion pipeline tuned for character skin on high-end iPhone-class GPUs.
This is not a replacement for Filament's existing `subsurface` model. It is a separate,
skin-specific path with a narrower authoring surface, clearer runtime behavior, and a mobile-first
quality strategy.

The first implementation targets opaque character skin only. It includes a dedicated sample app
that loads assets from `assets/models/character` and demonstrates the new pipeline with quality
switches and parameter tuning controls.

## Motivation

Filament currently ships an approximate `subsurface` shading model, but the implementation in
`shaders/src/surface_shading_model_subsurface.fs` is a local BTDF approximation added to the
standard BRDF. It does not provide a profile-based diffusion pipeline and cannot produce the
cross-pixel light transport required for convincing skin rendering.

Character skin has different requirements from generic translucent materials:

- diffusion across neighboring pixels matters more than a per-light local term
- specular detail must remain crisp while diffuse transport softens
- thickness response around ears, noses, and lips must be visible
- the implementation must fit a mobile-first budget

These constraints make a dedicated `skin` model the right architectural choice. Reusing the
existing `subsurface` model would couple a skin-specific renderer feature to a broader material
model that is currently intentionally approximate.

## Goals

- Add a dedicated `skin` shading model to Filament's material system.
- Implement a Burley-style profile-based subsurface pipeline as a post-lighting renderer feature.
- Optimize for high-end iPhone GPUs first, with explicit quality tiers and clean fallbacks.
- Preserve high-frequency specular detail by diffusing only the skin diffuse transport.
- Support character skin assets stored in `assets/models/character`.
- Provide a dedicated sample app that validates the full pipeline end to end.

## Non-Goals

- Replacing or redesigning the existing `subsurface` shading model.
- Supporting arbitrary translucent materials such as wax, jade, marble, or leaves in v1.
- Shipping a general-purpose many-profile authoring framework in v1.
- Supporting transparent skin materials in v1.
- Solving deep volumetric transmission, path-traced SSS parity, or offline Burley equivalence.
- Extending the feature to hair, teeth, eyes, or cloth in the first implementation.

## References

- Filament material and rendering documentation:
  - https://google.github.io/filament/Filament.md.html
  - https://google.github.io/filament/main/materials.html#materialmodels/subsurfacemodel
  - https://google.github.io/filament/notes/framegraph.html
- Current Filament implementation seams:
  - `shaders/src/surface_shading_model_subsurface.fs`
  - `shaders/src/surface_shading_lit.fs`
  - `libs/filamat/include/filamat/MaterialBuilder.h`
- External prior art:
  - Unreal Engine Subsurface Profile Shading Model:
    https://dev.epicgames.com/documentation/en-us/unreal-engine/subsurface-profile-shading-model-in-unreal-engine
  - Unity HDRP Diffusion Profile:
    https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition/manual/Diffusion-Profile.html
  - Christensen and Burley normalized diffusion / approximate BSSRDF work:
    https://graphics.pixar.com/library/ApproxBSSRDF/

## User-Facing Design

### New shading model

Add a new material shading model named `skin`.

This model is distinct from:

- `lit`, which remains the general-purpose standard model
- `subsurface`, which remains Filament's existing approximate translucent path
- `cloth`, which remains specialized for fabric-like response

The `skin` model exists to signal both authoring intent and runtime treatment. Choosing `skin`
means the renderer will route part of the shaded result through the skin diffusion pipeline.

### Material inputs

The v1 `skin` model should expose a compact, skin-specific set of inputs:

- `baseColor`
- `roughness`
- `normal`
- `thickness`
- `skinMask`
- `skinScatterDistance`
- `skinScatterStrength`
- `skinScatterTint`

V1 assumes one built-in analytic skin diffusion profile shared by all `skin` materials. The
per-material controls above modulate that built-in profile; they do not select arbitrary profile
assets or switch among multiple profile families.

Optional future inputs such as multi-lobe profiles, epidermal / subdermal separation, or profile
assets are intentionally deferred.

### Sample app

Add a dedicated sample app for the pipeline. It should not be folded into `material_sandbox`,
because the feature needs a controlled end-to-end validation surface.

The sample app should:

- load the character asset set from `assets/models/character`
- expose `off`, `cheap`, and `full` quality modes
- provide before / after comparison of diffusion enabled vs disabled
- provide controls for thickness, scatter distance, strength, and tint
- show that specular highlights remain sharp while diffuse transport softens

## Architecture

The recommended architecture is a dedicated `skin` shading model plus a renderer-owned
screen-space diffusion pass. This keeps the high-frequency lighting logic in the main shading path
and moves the low-frequency skin transport into a post-lighting stage where it can operate across
neighboring pixels.

This is the best fit for Filament because:

- the current `subsurface` implementation is local, not profile-based
- skin requires cross-pixel diffusion to look convincing
- Filament already has a FrameGraph-based renderer suitable for inserting a post-lighting stage
- a mobile-first implementation can scale independently from material authoring

The design explicitly avoids treating Burley diffusion as just another per-light BRDF tweak.
For Filament, Burley skin should be a renderer feature with material support, not merely a new
formula inside `surface_shading_model_subsurface.fs`.

## Component Breakdown

### 1. Material and compiler front-end

Add `skin` to the material enums, material parser, material compiler, shader generation logic, and
language bindings that currently enumerate shading models.

This includes:

- material DSL support for `shadingModel : skin`
- generated shader defines or variants required to compile the skin path
- `filamat` and `matc` support
- Android and other bindings that mirror shading model enums

The `skin` model should remain narrowly scoped. It is not intended to become a second generic
subsurface bucket.

### 2. Lighting stage

The main lighting shaders continue to compute standard surface lighting, but the output is split
for skin materials.

For `skin` materials, the lighting stage should produce:

- untouched surface specular and other high-frequency terms
- a skin diffuse contribution intended for later diffusion
- a compact control payload needed by the diffusion pass

The control payload should include:

- skin mask
- thickness-derived radius scale
- scatter strength
- controls used to modulate the built-in skin diffusion profile

The guiding rule is simple: only low-frequency skin transport enters the diffusion pass. Specular,
clear coat, reflections, emissive, and other high-frequency terms remain outside that blur.

### 3. Burley diffusion pass

The Burley stage is implemented as a FrameGraph post-lighting pass.

For v1, the pass should be:

- screen-space
- separable
- mobile-aware
- limited to one built-in skin profile with compact per-material modulation controls

The pass should operate on the skin diffuse target only. Its radius should be derived from physical
scatter distance and projected into screen space so that the perceived effect remains stable across
camera distance changes.

For mobile-first performance, the preferred execution shape is:

- horizontal pass
- vertical pass
- optional half-resolution intermediate
- limited tap count
- edge protection sufficient to avoid obvious bleeding across silhouettes and strong depth changes

The implementation does not need to reproduce offline Burley exactly. It needs to behave like a
profile-driven skin diffusion model with stable, controllable real-time results.

### 4. Composite stage

After diffusion, the renderer recombines:

- blurred skin diffuse
- untouched specular
- reflections
- emissive
- post-lighting color terms

This stage must preserve the crisp appearance of pores, oily highlights, and other specular detail.
If the blur contaminates specular, the pipeline will fail its primary visual requirement.

### 5. Quality and fallback system

The renderer should support three explicit modes:

- `off`: bypass diffusion entirely
- `cheap`: reduced-cost mode, either via a smaller kernel, lower resolution, or a simplified path
- `full`: intended high-quality mobile setting for high-end iPhones

If the diffusion pass cannot run because of unsupported configuration or transient resource
allocation failure, Filament should degrade gracefully. The fallback path should be either the
current approximate `subsurface` response or a simplified non-diffused skin response. Material
compilation must still succeed even when the runtime cannot execute the full pipeline.

## Data Flow

The end-to-end frame flow for `skin` materials is:

1. Material author chooses `shadingModel : skin`.
2. Material compiler generates the skin shader path and required parameters.
3. Main lighting stage shades the surface.
4. Skin diffuse transport is written into an intermediate SSS input target.
5. Per-pixel control data is written for radius / strength / masking decisions.
6. FrameGraph runs the Burley-style separable diffusion pass.
7. Composite stage recombines blurred skin diffuse with untouched specular and other lighting.
8. Final shaded skin result proceeds through Filament's normal post-lighting flow.

## Performance Strategy

The primary platform target is high-end iPhone hardware. That means bandwidth and intermediate
target count matter more than maximum generality.

v1 performance rules:

- optimize for Metal on iPhone first
- keep the number of extra render targets minimal
- prefer half-resolution intermediates when quality remains acceptable
- keep the kernel compact and separable
- support runtime quality switching without material recompilation
- avoid an asset-driven many-profile system in the first version

The first implementation should prioritize visual stability and predictable cost over feature
breadth.

## Unsupported Cases In V1

The following are explicitly out of scope for the first version:

- transparent or blended skin materials
- multiple simultaneously active artist-authored diffusion profile assets
- exact visual parity with desktop-oriented Unreal or Unity implementations
- hair, eyes, teeth, and cloth sharing the same pipeline
- exact offline Burley or path-traced SSS reproduction
- broad backend tuning across every platform before Metal mobile quality is validated

## Error Handling

The pipeline should fail predictably and degrade cleanly.

Required behavior:

- missing thickness maps resolve to a default constant thickness
- unsupported runtime configurations skip the diffusion pass without crashing
- resource allocation failure during pass setup skips diffusion and emits a debug diagnostic
- material compilation succeeds even if the runtime later selects a reduced mode

Debug logging is sufficient for v1. There is no requirement to surface detailed user-facing error
messages for sample or engine consumers.

## Testing Strategy

### Compiler and API coverage

Add tests that verify:

- `skin` shading model enum plumbing is correct
- material parsing and compilation succeed for `shadingModel : skin`
- generated variants include the intended shader defines and code paths
- bindings that expose shading models remain in sync

### Renderer coverage

Add tests that verify:

- the diffusion pass is inserted into the FrameGraph for skin materials
- the pass is bypassed correctly for `off` mode
- quality switching selects the expected runtime path
- resource lifetime and intermediate target usage are stable
- fallback behavior is correct when the pass is unavailable

### Visual validation

Use the character sample asset in `assets/models/character` as the primary validation surface.

Validation criteria:

- visible softening of diffuse transport around thin or backlit facial areas
- preserved specular sharpness
- stable silhouettes without obvious diffusion halos
- clear thickness response
- acceptable quality and frame cost on the target iPhone hardware class

If practical, add render-diff coverage for at least one canonical camera and lighting setup.

## Example App Requirements

Add a new dedicated sample app for the skin pipeline.

The sample should include:

- the character asset from `assets/models/character`
- a reference lighting setup suitable for evaluating skin
- UI toggles for `off`, `cheap`, and `full`
- controls for scatter distance, strength, tint, and thickness scaling
- a comparison mode that isolates the diffusion contribution

The example app serves two roles:

- user-facing documentation for the new feature
- a stable visual regression surface for future renderer changes

## Implementation Notes

Expected implementation seams include:

- shader files under `shaders/src/`
- material compiler and generator logic under `libs/filamat/`
- renderer integration through Filament's FrameGraph-managed post-lighting passes
- sample registration and build integration under `samples/`

The existing files most likely to inform the implementation are:

- `shaders/src/surface_shading_model_subsurface.fs`
- `shaders/src/surface_shading_lit.fs`
- `libs/filamat/include/filamat/MaterialBuilder.h`
- `assets/models/character`

The exact set of engine files will depend on how shading-model dispatch and post-lighting passes
are currently wired, but the architecture above is intended to minimize broad renderer disruption.

## Risks

- The mobile blur budget may be tighter than expected once combined with Filament's existing post
  stack.
- Thickness inputs in the character asset may need preprocessing or remapping before they produce
  stable skin results.
- Edge protection that is too weak will create halos; too strong will suppress the desired effect.
- If the pipeline leaks specular into the diffusion target, skin will immediately look wrong.

These are implementation and tuning risks, not reasons to change the overall architecture.

## Recommended Next Step

Write an implementation plan that breaks the work into:

- material/compiler plumbing for `skin`
- shader and lighting-path split for skin diffuse vs specular
- FrameGraph diffusion and composite passes
- quality-tier and fallback wiring
- dedicated character sample app
- testing and visual validation on target mobile hardware
