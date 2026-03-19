# Burley SSS Comparison Report

This capture set is the Filament side of the repeatable Unreal-vs-Filament Burley comparison workflow.

- Engine commit: `1d3eed9d0`
- Preset: `Unreal Burley Reference`
- Metadata: [`metadata.json`](./metadata.json)
- Baseline render settings: direct light only, fixed viewpoints, TAA disabled, bloom disabled, DOF disabled, SSR disabled

## Parameter Mapping

| Unreal Burley term | Filament current mapping | Status |
| --- | --- | --- |
| Surface Albedo | `baseColor` material parameter | Approximate |
| Mean Free Path Color | material `subsurfaceColor`, with view color acting as a multiplier/default | Approximate |
| Mean Free Path Distance | material `scatteringDistance`, with view distance acting as a multiplier/default | Approximate |
| World Unit Scale | folded into the authored/sample scattering distance | Approximate |
| Tint | per-pixel Burley params target | Implemented |
| Boundary Color Bleed | tracked in sample metadata only | Missing in engine |
| Transmission block | separate thin-region transmission lift driven by thickness and Burley tint | Approximate |
| Dual Specular | untouched default lit specular path | Missing |

## Artifact Grid

Capture filenames are deterministic and overwrite previous runs for the same preset.

Current viewpoint: `front_lit`

- `final.png`
- `sss_influence.png`
- `sss_membership.png`
- `pre_blur_diffuse.png`
- `post_blur_diffuse.png`
- `terminator_window.png`
- `band_mask.png`
- `transmission.png`

## Gap Matrix

| Behavior | Unreal reference | Current Filament | Visible symptom | Root cause guess | Fix required |
| --- | --- | --- | --- | --- | --- |
| diffuse/specular separation | Diffuse lighting is filtered, specular is preserved and recombined later. | Implemented via auxiliary diffuse MRT and unblurred specular recombine. | Specular highlights no longer smear with the SSS blur. | Needed explicit diffuse capture in the color pass. | Keep validating against multi-light cases and dual-spec materials. |
| pre-blur setup buffer contents | Setup stores Burley-ready diffuse plus profile metadata. | Diffuse.rgb plus membership alpha is stored alongside per-pixel Burley tint/radius and thickness context. | Blur now reads real per-pixel Burley params instead of one shared view-global profile. | Broad interior scatter can be driven from material data rather than a contour seed alone; profile ids and boundary bleed are still not part of the payload. | Only add profile id / bleed if mixed-profile scenes actually require it. |
| per-pixel SSS membership | Per-pixel profile membership is carried through the setup pass. | Implemented as per-pixel membership alpha in the SSS diffuse buffer. | Membership debug view matches eligible SSS pixels. | Single scalar membership bit is available, but no profile differentiation yet. | Promote membership to profile-aware ids for multi-material scenes. |
| per-pixel scattering parameters | Mean free path and tint are resolved per pixel via the subsurface profile system. | Implemented via per-pixel auxiliary Burley params, with View::SubsurfaceScatteringOptions acting as a multiplier/default. | Different Burley materials can now carry different radius and tint values through the same blur pass, though profile ids are still absent if they overlap in screen space. | Current scope targets Filament-first material parity rather than full Unreal profile assets. | Validate mixed-material scenes before adding profile-id rejection. |
| world-unit kernel scaling | Kernel radius derives from mean free path distance and world unit scale. | Projected radius is still screen-space, but now scaled by per-pixel scattering distance before the view-global multiplier. | Sample presets map more directly into the engine path, though no explicit world-unit field is stored in the SSS payload and scene-scale mismatch can still drift if radius authoring is inconsistent. | World unit scale is still folded into material/sample setup rather than stored independently. | Only add an explicit world-unit field if real content shows scale inconsistency. |
| Burley kernel shape | Burley uses normalized diffusion with profile-driven adaptive sampling. | A single-radius separable Burley kernel is used. | Band shape is plausible but not yet profile-accurate under all presets. | Current kernel is scalar and global rather than profile-driven. | Match Unreal's per-profile scaling before tuning sample count or weights. |
| bilateral depth rejection | Depth-aware rejection prevents leaking across discontinuities. | Implemented in the blur pass. | Geometric borders stay sharper than the first prototype. | Depth threshold is still tied to the global scattering distance. | Parameterize the rejection threshold per pixel once profile radii are stored. |
| bilateral normal rejection | Normals are used to reject taps across sharp shading changes. | Implemented with the stored shaded normal buffer. | SSS weighting now follows the same normal-map detail as the material shading path. | A dedicated SSS normal target is carried from the color pass into blur and recombine. | Validate the stored-normal path on thin detail and grazing angles. |
| recombine formula | Burley adds scattered diffuse back to untouched non-SSS lighting. | Implemented as original diffuse plus positive scattering delta tinted by the per-pixel Burley params, with a separate thin-region transmission lift. | Final shading reads less like a rim blur and more like broad interior diffusion plus backlit translucency, fixing the earlier contour-specific gating problem. | Transmission is still approximated from thickness and geometry rather than a full profile model. | Tune transmission strength and mixed-material behavior against captures before adding more profile complexity. |
| base color application point | Surface albedo participates at setup/recombine according to the profile. | Base color currently still follows Filament's material path rather than an Unreal-equivalent profile block. | Reference albedo is close, but profile coupling is incomplete. | Surface albedo is not stored separately in the SSS setup data. | Add explicit Burley profile mapping for surface albedo versus tint. |
| boundary color bleed | Boundary bleed is profile-aware and configurable. | Not implemented. | Different SSS materials would hard-stop or incorrectly mix. | No profile id or boundary tuning reaches the blur pass today. | Add profile-aware bleed weighting once profile ids exist. |
| transmission / thickness lighting | Transmission is handled separately from lateral surface blur. | Implemented as a distinct recombine-stage backlight / silhouette lift driven by thickness, geometry, and Burley tint. | Ears and other thin regions can now pick up a separate translucent glow from the main blur lobe, though it is still an approximation rather than full Unreal transmission parity. | No dedicated thickness texture or full profile transmission block exists yet. | Tune against thin-region captures before deciding whether a richer transmission payload is necessary. |
| multi-material profile handling | Multiple subsurface profiles can coexist in one scene. | Not implemented. | Different materials would share one global blur profile. | No profile id cache or per-pixel profile selection exists. | Store profile ids and reject or bleed taps based on profile compatibility. |
| dual-spec preservation | Dual specular is a separate profile feature layered after SSS. | Not implemented. | Skin-like glints will still differ from Unreal even when the band matches. | Current work isolates diffuse scattering from the specular path only. | Keep it isolated until diffuse Burley parity is acceptable. |
| half-res / full-res behavior | Unreal can switch to separable or half-res modes based on quality settings. | Only the full-resolution path is present. | Performance parity is not representative yet. | No half-res or AFIS fallback has been added. | Add after full-resolution parity is stable. |
| TAA interaction | Burley is evaluated with TAA-aware quality modes. | Comparison workflow disables TAA for baseline captures. | Temporal behavior is intentionally out of scope for the baseline report. | Need stable direct-light captures before testing temporal accumulation. | Re-enable TAA only after the direct-light capture set matches well. |

## Next Action

Validate the new per-pixel Burley payload on mixed-material scenes, then decide whether profile ids, boundary color bleed, or richer transmission controls are the next highest-value parity step.
