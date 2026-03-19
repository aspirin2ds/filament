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

Each comparison run captures the same artifact list for every fixed viewpoint:

- `final`
- `diffuse_only`
- `specular_only`
- `sss_influence`
- `sss_membership`
- `depth`
- `normal`
- `pre_blur_diffuse`
- `post_blur_diffuse`

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
| Mean Free Path Color | `subsurfaceColor` | Approximate only. |
| Mean Free Path Distance | `scatteringDistance` | Sample maps it as `meanFreePathDistance * worldUnitScale`. |
| World Unit Scale | tracked in sample metadata only | Not consumed by the engine blur path yet. |
| Tint | tracked in sample metadata only | Missing from the engine Burley setup buffer. |
| Boundary Color Bleed | tracked in sample metadata only | Missing in the engine blur pass. |
| Transmission block | thickness parameter exists | Burley transmission parity not implemented. |
| Dual Specular | regular Filament specular path | Deliberately left out of the blur work for now. |

## Current Gap Matrix

| Behavior | Unreal reference | Current Filament | Visible symptom | Root cause guess | Fix required |
| --- | --- | --- | --- | --- | --- |
| Diffuse / specular separation | Diffuse is filtered, specular is preserved and recombined later. | Implemented via SSS diffuse MRT and sharp specular recombine. | Highlights stay sharp instead of smearing. | Needed a dedicated diffuse capture buffer. | Keep validating with more than one light and with glossy materials. |
| Pre-blur setup buffer contents | Setup stores diffuse plus profile metadata. | Stores diffuse RGB plus membership alpha only. | Blur targets the right regions, but all SSS pixels still share one Burley profile. | Auxiliary buffer does not carry per-pixel profile data yet. | Extend the setup buffer to encode profile id, tint, and blur radius inputs. |
| Per-pixel SSS membership | Per-pixel profile membership is available for setup and recombine. | Implemented as per-pixel membership alpha. | `SSS Membership` view is now explicit and repeatable. | Only a scalar membership bit is present. | Promote membership to profile-aware ids. |
| Per-pixel scattering parameters | Mean free path and tint are resolved per pixel. | Still global through `SubsurfaceScatteringOptions`. | Multi-material scenes cannot match Unreal's profile behavior. | Blur shader still reads one view-global distance and tint. | Store per-pixel scattering scale and tint in the SSS setup target. |
| World-unit kernel scaling | Radius depends on mean free path distance and world unit scale. | Sample tracks world-unit scale, engine ignores it. | Reference preset can drift when scene scale changes. | No Burley profile block is passed into the post-process pass. | Fold world-unit scale into the stored per-pixel profile parameters. |
| Burley kernel shape | Adaptive/profile-driven Burley sampling. | Single-radius separable kernel. | Terminator band is plausible but not fully profile-accurate. | Scalar global kernel instead of profile-aware sampling. | Match profile-driven scaling before sample-count tuning. |
| Bilateral depth rejection | Rejects depth-incompatible taps. | Implemented. | Hard geometric borders remain sharper. | Threshold still depends on the global radius. | Parameterize threshold from per-pixel profile radius. |
| Bilateral normal rejection | Rejects taps across sharp shading changes. | Implemented with the stored shaded normal buffer. | Blur and recombine now follow the same normal-map detail as the material shading path. | A dedicated SSS normal target is carried from the color pass into the blur pass. | Validate the stored-normal path on thin detail and grazing angles. |
| Recombine formula | Adds scattered diffuse back to untouched non-SSS content. | Implemented as `original diffuse + positive scattering delta * subsurfaceColor`. | Prevents whole-model washout and makes the band visible in the final path. | Earlier prototype used the fully blurred diffuse directly. | Revisit delta scale once profile tint becomes per pixel. |
| Base color application point | Surface albedo is integrated consistently with the profile. | Still follows Filament's material path rather than a dedicated Burley profile block. | Albedo matching is approximate, not profile-accurate. | Surface albedo is not stored in setup metadata. | Introduce explicit Burley profile mapping for albedo versus tint. |
| Boundary color bleed | Configurable and profile-aware. | Missing. | Different SSS materials would either stop abruptly or mix incorrectly. | No profile id and no bleed parameter in the blur pass. | Add profile-aware bleed weighting. |
| Transmission / thickness lighting | Separate Burley transmission path. | Thickness exists, Burley transmission parity missing. | Backscatter cannot be compared meaningfully yet. | Current work focused on the screen-space band first. | Add dedicated transmission implementation and capture views for it. |
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

1. Add per-pixel Burley profile storage to the SSS setup buffer.
2. Move world-unit scaling and tint out of `View::SubsurfaceScatteringOptions`.
3. Validate the stored normal path on thin detail and grazing-angle captures.
4. Add profile ids and boundary-color-bleed-aware bilateral weighting.
5. Implement Burley transmission as a separate tracked feature.
6. Revisit half-res and TAA behavior only after the direct-light full-resolution baseline matches.
