# Unreal Engine SSS File Reference

Cached list of all SSS-related files in `../UnrealEngine` for quick lookup.
Base path: `/Users/aspirin2ds/Workspace/github/UnrealEngine`

## Core Burley SSS Shaders (read these first)

- `Engine/Shaders/Private/BurleyNormalizedSSSCommon.ush` — Burley profile math, scaling factors, transmission formulas
- `Engine/Shaders/Private/SubsurfaceBurleyNormalized.ush` — AFIS disk sampling, adaptive counts, bilateral weighting, `BurleyNormalizedSS()`
- `Engine/Shaders/Private/PostProcessSubsurface.usf` — Setup pass, `ReconstructLighting()`, `SubsurfaceRecombinePS()`
- `Engine/Shaders/Private/PostProcessSubsurfaceCommon.ush` — Shared types/defines for SSS post-process
- `Engine/Shaders/Private/PostProcessSubsurfaceTile.usf` — Tile classification for SSS

## Supporting Shaders

- `Engine/Shaders/Private/SubsurfaceProfileCommon.ush` — Profile texture access, shared helpers
- `Engine/Shaders/Private/SeparableSSS.ush` — Legacy separable SSS kernel (non-Burley path)
- `Engine/Shaders/Private/TransmissionCommon.ush` — Transmission lighting helpers
- `Engine/Shaders/Private/TransmissionThickness.ush` — Thickness-based transmission
- `Engine/Shaders/Private/bend_sss_gpu.ush` — Bend SSS shadow integration
- `Engine/Shaders/Private/SSProfilePreIntegratedMobile.usf` — Mobile pre-integrated SSS
- `Engine/Shaders/Private/Substrate/SubstrateSubsurface.ush` — Substrate subsurface path
- `Engine/Shaders/Private/PathTracing/Material/PathTracingSubsurfaceProfile.ush` — Path tracing SSS
- `Engine/Shaders/Private/ShadingModels.ush` — Shading model selection (contains SSS branches)
- `Engine/Shaders/Private/ShadingModelsMaterial.ush` — Material shading model defines
- `Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapTransmissionCommon.ush` — VSM transmission

## C++ Post-Process / Renderer

- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.cpp` — SSS pass setup, dispatch, parameter binding
- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessSubsurface.h` — SSS pass declarations
- `Engine/Source/Runtime/Renderer/Private/SubsurfaceTiles.cpp` — Tile-based culling for SSS
- `Engine/Source/Runtime/Renderer/Private/SubsurfaceTiles.h` — Tile culling declarations

## C++ Engine / Profile

- `Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h` — `FSubsurfaceProfileStruct` definition (all per-profile parameters)
- `Engine/Source/Runtime/Engine/Private/Rendering/SubsurfaceProfile.cpp` — Profile management, GPU texture upload
- `Engine/Source/Runtime/Engine/Private/Rendering/BurleyNormalizedSSS.h` — C++ Burley helpers
- `Engine/Source/Runtime/Engine/Private/Rendering/BurleyNormalizedSSS.cpp` — C++ Burley math (CPU-side profile evaluation)

## Renderer Integration (files that reference SSS)

- `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp` — Deferred pipeline SSS pass insertion
- `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp` — Scene rendering SSS hooks
- `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp` — Light rendering with transmission
- `Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp` — Shadow + SSS interaction
- `Engine/Source/Runtime/Renderer/Private/ShadowRendering.h`
- `Engine/Source/Runtime/Renderer/Private/MobileBasePassRendering.cpp` — Mobile SSS path
- `Engine/Source/Runtime/Renderer/Private/MobileShadingRenderer.cpp` — Mobile SSS renderer
- `Engine/Source/Runtime/Renderer/Private/PathTracing.cpp` — Path tracing SSS
- `Engine/Source/Runtime/Renderer/Private/ScenePrivate.h` — Scene state for SSS
- `Engine/Source/Runtime/Renderer/Private/SceneViewState.cpp` — View state (temporal SSS history)
- `Engine/Source/Runtime/Renderer/Private/Shadows/bend_sss_cpu.h` — Bend SSS CPU helpers
- `Engine/Source/Runtime/RenderCore/Public/GBufferInfo.h` — G-Buffer layout (SSS channel allocation)
