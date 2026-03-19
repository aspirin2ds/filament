# Burley SSS Comparison Report

This capture set is the Filament side of the repeatable Unreal-vs-Filament Burley comparison workflow.

- Engine commit: `ca4ed0859`
- Preset: `Unreal Burley Reference`
- Metadata: [`metadata.json`](./metadata.json)
- Baseline render settings: direct light only, fixed viewpoints, TAA disabled, bloom disabled, DOF disabled, SSR disabled

## Parameter Mapping

| Unreal Burley term | Filament current mapping | Status |
| --- | --- | --- |
| Surface Albedo | `baseColor` material parameter | Approximate |
| Mean Free Path Color | `subsurfaceColor` sample parameter | Approximate |
| Mean Free Path Distance | `scatteringDistance = meanFreePathDistance * worldUnitScale` | Approximate |
| World Unit Scale | tracked in sample metadata only | Missing in engine |
| Tint | tracked in sample metadata only | Missing in engine |
| Boundary Color Bleed | tracked in sample metadata only | Missing in engine |
| Transmission block | thickness exists, Burley transmission parity missing | Missing |
| Dual Specular | untouched default lit specular path | Missing |

## Artifact Grid

Capture filenames are deterministic and overwrite previous runs for the same preset.

Current viewpoint: `front_lit`

- `final.png`
- `diffuse_only.png`
- `specular_only.png`
- `sss_influence.png`
- `sss_membership.png`
- `depth.png`
- `normal.png`
- `pre_blur_diffuse.png`
- `post_blur_diffuse.png`

## Gap Matrix

| Behavior | Unreal reference | Current Filament | Visible symptom | Root cause guess | Fix required |
| --- | --- | --- | --- | --- | --- |
| diffuse/specular separation | Diffuse lighting is filtered, specular is preserved and recombined later. | Implemented via auxiliary diffuse MRT and unblurred specular recombine. | Specular highlights no longer smear with the SSS blur. | Needed explicit diffuse capture in the color pass. | Keep validating against multi-light cases and dual-spec materials. |
| pre-blur setup buffer contents | Setup stores Burley-ready diffuse plus profile metadata. | Diffuse.rgb plus membership alpha is stored, but not the full profile payload. | Blur can run on the right pixels but still relies on view-global Burley parameters. | Only one auxiliary MRT is used today. | Extend setup to pack per-pixel profile id, tint, and radius inputs. |
| per-pixel SSS membership | Per-pixel profile membership is carried through the setup pass. | Implemented as per-pixel membership alpha in the SSS diffuse buffer. | Membership debug view matches eligible SSS pixels. | Single scalar membership bit is available, but no profile differentiation yet. | Promote membership to profile-aware ids for multi-material scenes. |
| per-pixel scattering parameters | Mean free path and tint are resolved per pixel via the subsurface profile system. | Still view-global through View::SubsurfaceScatteringOptions. | All SSS pixels share one scattering distance and one tint in the blur pass. | Current pipeline does not serialize Burley profile data into the setup buffer. | Store per-pixel scattering scale, tint, and profile id in the auxiliary target. |
| world-unit kernel scaling | Kernel radius derives from mean free path distance and world unit scale. | Only a single global projected radius is used. | Reference preset can drift when asset scale changes. | World unit scale is tracked in the sample but not consumed by the engine path. | Move world-unit scaling into the per-pixel Burley parameter block. |
| Burley kernel shape | Burley uses normalized diffusion with profile-driven adaptive sampling. | A single-radius separable Burley kernel is used. | Band shape is plausible but not yet profile-accurate under all presets. | Current kernel is scalar and global rather than profile-driven. | Match Unreal's per-profile scaling before tuning sample count or weights. |
| bilateral depth rejection | Depth-aware rejection prevents leaking across discontinuities. | Implemented in the blur pass. | Geometric borders stay sharper than the first prototype. | Depth threshold is still tied to the global scattering distance. | Parameterize the rejection threshold per pixel once profile radii are stored. |
| bilateral normal rejection | Normals are used to reject taps across sharp shading changes. | Implemented with the stored shaded normal buffer. | SSS weighting now follows the same normal-map detail as the material shading path. | A dedicated SSS normal target is carried from the color pass into blur and recombine. | Validate the stored-normal path on thin detail and grazing angles. |
| recombine formula | Burley adds scattered diffuse back to untouched non-SSS lighting. | Implemented as original diffuse plus positive scattering delta tinted by subsurface color. | Avoids full-model washout and makes the band visible in the final view. | Previous path used fully blurred diffuse directly. | Validate the delta scale against Unreal's profile tuning once per-pixel tint arrives. |
| base color application point | Surface albedo participates at setup/recombine according to the profile. | Base color currently still follows Filament's material path rather than an Unreal-equivalent profile block. | Reference albedo is close, but profile coupling is incomplete. | Surface albedo is not stored separately in the SSS setup data. | Add explicit Burley profile mapping for surface albedo versus tint. |
| boundary color bleed | Boundary bleed is profile-aware and configurable. | Not implemented. | Different SSS materials would hard-stop or incorrectly mix. | No profile id or boundary tuning reaches the blur pass today. | Add profile-aware bleed weighting once profile ids exist. |
| transmission / thickness lighting | Transmission is handled separately from lateral surface blur. | Thickness exists as a material input, but Burley transmission parity is not implemented. | Backscatter behavior is not comparable yet. | Current work focused on the screen-space surface band first. | Add a dedicated transmission stage and compare it independently from the blur. |
| multi-material profile handling | Multiple subsurface profiles can coexist in one scene. | Not implemented. | Different materials would share one global blur profile. | No profile id cache or per-pixel profile selection exists. | Store profile ids and reject or bleed taps based on profile compatibility. |
| dual-spec preservation | Dual specular is a separate profile feature layered after SSS. | Not implemented. | Skin-like glints will still differ from Unreal even when the band matches. | Current work isolates diffuse scattering from the specular path only. | Keep it isolated until diffuse Burley parity is acceptable. |
| half-res / full-res behavior | Unreal can switch to separable or half-res modes based on quality settings. | Only the full-resolution path is present. | Performance parity is not representative yet. | No half-res or AFIS fallback has been added. | Add after full-resolution parity is stable. |
| TAA interaction | Burley is evaluated with TAA-aware quality modes. | Comparison workflow disables TAA for baseline captures. | Temporal behavior is intentionally out of scope for the baseline report. | Need stable direct-light captures before testing temporal accumulation. | Re-enable TAA only after the direct-light capture set matches well. |

## Next Action

Promote the setup buffer from `diffuse + membership` to full per-pixel Burley profile data so kernel radius, tint, and boundary behavior stop depending on view-global options.
