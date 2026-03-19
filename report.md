# Burley SSS Gap Analysis Workflow

This document is the source-controlled counterpart to `sample_sss_burley`. The sample now emits a
deterministic Filament capture set and a per-run markdown report under
`captures/sss_burley/<preset>/`. This file defines the workflow, the parameter mapping, and the
current gap matrix so Burley parity work can move forward in a repeatable way.

## Canonical Comparison Setup

Use the Burley sample as the Filament harness and a matched Unreal scene as the reference.

- Mesh: Suzanne (`generated/resources/monkey.h`)
- Camera framing: fixed sample viewpoints
- Viewpoints:
  - `front_lit`
  - `three_quarter`
  - `grazing`
  - `thin_region`
- Lighting baseline:
  - one directional light
  - IBL intensity forced to `0.0` for the deterministic capture set
  - TAA disabled
  - bloom disabled
  - DOF disabled
  - SSR disabled
- Reference preset:
  - `Unreal Burley Reference`
  - values mirrored from Unreal's Burley profile UI screenshot

## Filament Capture Artifacts

Each comparison run captures the focused SSS debug artifact list for every fixed viewpoint:

- `final`
- `sss_influence`
- `sss_membership`
- `pre_blur_diffuse`
- `post_blur_diffuse`
- `terminator_window`
- `band_mask`
- `transmission`

The sample writes:

- `metadata.json`
- `comparison_report.md`
- one PNG per `(viewpoint, artifact)` pair

Capture names are deterministic and overwrite the previous run for that preset, which makes visual
diffing straightforward.

## Parameter Mapping

| Unreal Burley term | Filament mapping today | Notes |
| --- | --- | --- |
| Surface Albedo | `baseColor` | Close, but not yet stored as a dedicated Burley profile field. |
| Mean Free Path Color | material `subsurfaceColor`, with the view value acting as a multiplier/default | Approximate only. |
| Mean Free Path Distance | material `scatteringDistance`, with the view value acting as a multiplier/default | Sample still maps it as `meanFreePathDistance * worldUnitScale`. |
| World Unit Scale | folded into the authored/sample scattering distance | Not stored as an explicit engine-side Burley field. |
| Tint | stored in the per-pixel SSS params target | Implemented. |
| Boundary Color Bleed | tracked in sample metadata only | Missing in the engine blur pass. |
| Transmission block | separate thin-region transmission lift uses thickness + geometry | Approximate parity only. |
| Dual Specular | regular Filament specular path | Deliberately left out of the blur work for now. |

## Current Gap Matrix

| Behavior | Unreal reference | Current Filament | Visible symptom | Root cause guess | Fix required |
| --- | --- | --- | --- | --- | --- |
| Diffuse / specular separation | Diffuse is filtered, specular is preserved and recombined later. | Implemented via SSS diffuse MRT and sharp specular recombine. | Highlights stay sharp instead of smearing. | Needed a dedicated diffuse capture buffer. | Keep validating with more than one light and with glossy materials. |
| Pre-blur setup buffer contents | Setup stores diffuse plus profile metadata. | Stores diffuse RGB plus membership alpha, plus a separate per-pixel params target for tint/radius and thickness context in the normal target alpha. | Blur is now driven by material data instead of a single shared profile approximation. | Profile ids and boundary bleed are still absent from the payload. | Validate mixed-material scenes before adding profile ids or bleed. |
| Per-pixel SSS membership | Per-pixel profile membership is available for setup and recombine. | Implemented as per-pixel membership alpha. | `SSS Membership` view is now explicit and repeatable. | Only a scalar membership bit is present. | Promote membership to profile-aware ids. |
| Per-pixel scattering parameters | Mean free path and tint are resolved per pixel. | Implemented through the SSS params target, with `SubsurfaceScatteringOptions` acting as a multiplier/default. | Mixed-material scenes are now possible without one shared blur profile. | Profile identity is still missing when different Burley materials overlap in screen space. | Add profile ids only if real content shows cross-material bleeding problems. |
| World-unit kernel scaling | Radius depends on mean free path distance and world unit scale. | Engine now consumes per-pixel radius, but world-unit scale is still pre-folded into authored/sample values. | Scene-scale mismatch can still drift if radius authoring is inconsistent. | There is still no explicit world-unit scalar in the runtime payload. | Only add an explicit world-unit field if real content exposes scale drift. |
| Burley kernel shape | Adaptive/profile-driven Burley sampling. | Single-radius separable kernel. | Terminator band is plausible but not fully profile-accurate. | Scalar global kernel instead of profile-aware sampling. | Match profile-driven scaling before sample-count tuning. |
| Bilateral depth rejection | Rejects depth-incompatible taps. | Implemented. | Hard geometric borders remain sharper. | Threshold now uses the per-pixel radius but still assumes one center-driven blur neighborhood. | Revisit sample-vs-center radius weighting only if wide profile mismatches appear. |
| Bilateral normal rejection | Rejects taps across sharp shading changes. | Implemented with the stored shaded normal buffer. | Blur and recombine now follow the same normal-map detail as the material shading path. | A dedicated SSS normal target is carried from the color pass into the blur pass. | Validate the stored-normal path on thin detail and grazing angles. |
| Recombine formula | Adds scattered diffuse back to untouched non-SSS content. | Implemented as `original diffuse + positive scattering delta * per-pixel tint`, plus a separate thin-region transmission lift. | Final shading reads less like a contour blur and more like broad interior diffusion plus backlit translucency. | The current transmission term is still an approximation driven by thickness and geometry. | Tune transmission strength and thin-region behavior against captures before adding more payload. |
| Base color application point | Surface albedo is integrated consistently with the profile. | Still follows Filament's material path rather than a dedicated Burley profile block. | Albedo matching is approximate, not profile-accurate. | Surface albedo is not stored in setup metadata. | Introduce explicit Burley profile mapping for albedo versus tint. |
| Boundary color bleed | Configurable and profile-aware. | Missing. | Different SSS materials would either stop abruptly or mix incorrectly. | No profile id and no bleed parameter in the blur pass. | Add profile-aware bleed weighting. |
| Transmission / thickness lighting | Separate Burley transmission path. | Implemented as a recombine-stage backlight / silhouette term using thickness and Burley tint. | Thin features can now glow separately from the main lateral blur. | No dedicated thickness texture or full Unreal transmission block exists yet. | Tune against ear / nostril / jaw captures before deciding on a richer transmission payload. |
| Multi-material profile handling | Multiple profiles can coexist in one scene. | Missing. | One blur profile would apply to all SSS materials. | No profile id cache or profile-specific mask. | Store profile ids and validate taps by profile compatibility. |
| Dual-spec preservation | Dual specular is a separate profile feature. | Missing. | Skin glints will still differ after diffuse parity improves. | Only diffuse scattering parity is being targeted right now. | Isolate and add dual-spec after diffuse Burley parity is acceptable. |
| Half-res / full-res behavior | Full-res and half-res/AFIS paths exist. | Full-res only. | Performance comparisons are not representative yet. | No half-res path in Filament's SSS pass. | Add after full-resolution parity is stable. |
| TAA interaction | Burley is tuned with TAA-aware quality modes. | Baseline workflow disables TAA intentionally. | Temporal stability is not part of the baseline capture. | Direct-light parity needs to be solved first. | Re-enable TAA only after the fixed baseline looks correct. |

## How To Use The Workflow

1. Run `sample_sss_burley`.
2. Choose `Unreal Burley Reference`.
3. Click `Capture Comparison Set`.
4. Compare `captures/sss_burley/unreal_burley_reference/` against the matched Unreal captures.
5. Update the gap matrix with any newly observed mismatch before changing shader behavior.

## Prioritized Fix List

1. Validate the new per-pixel Burley payload on mixed-material scenes.
2. Tune the thin-region transmission term against ear and grazing-angle captures.
3. Decide whether profile ids are needed before adding boundary-color-bleed-aware weighting.
4. Validate the stored normal path on thin detail and grazing-angle captures.
5. Revisit half-res and TAA behavior only after the direct-light full-resolution baseline matches.
