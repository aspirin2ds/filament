# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is Filament?

Filament is a real-time physically based rendering (PBR) engine optimized for mobile. It supports Android, iOS, Linux, macOS, Windows, WebGL, and WebGPU with backends for OpenGL ES 3.0+/GL 4.1+, Vulkan, Metal, and WebGPU.

## Build Commands

```bash
# Desktop builds (macOS/Linux)
./build.sh debug                    # Debug build
./build.sh release                  # Release build
./build.sh -c debug                 # Clean + debug build
./build.sh -i release               # Build + install release

# Platform builds
./build.sh -p android release       # Android
./build.sh -p ios release           # iOS
./build.sh -p webgl release         # WebGL (requires EMSDK env var)

# Run tests
./build.sh -u debug                 # Build debug + run all unit tests

# Run a single test manually
./out/cmake-debug/libs/math/test_math
./out/cmake-debug/filament/test/test_filament

# CI-style builds
cd build/mac && printf "y" | ./build.sh presubmit-with-test
cd build/linux && printf "y" | ./build.sh presubmit

# Static analysis
bash test/code-correctness/test.sh

# Backend tests (OpenGL + Vulkan via Mesa)
bash test/backend/test.sh
```

Build output goes to `out/cmake-{debug,release}/`. Installed artifacts go to `out/{debug,release}/filament/`.

Key CMake flags: `FILAMENT_SUPPORTS_VULKAN`, `FILAMENT_SUPPORTS_METAL`, `FILAMENT_SUPPORTS_OPENGL`, `FILAMENT_BUILD_FILAMAT`, `FILAMENT_ENABLE_ASAN_UBSAN`.

## Architecture

### Core Engine (`filament/`)

The public API lives in `filament/include/filament/` with these key types:
- **Engine** — main entry point, creates and owns all resources
- **Renderer** — drives frame rendering (beginFrame → render → endFrame)
- **View** — binds a Scene + Camera + viewport + rendering options
- **Scene** — container for renderables and lights
- **Material / MaterialInstance** — shading model template and per-instance parameters

Implementation details are in `filament/src/details/` (prefixed with `F`, e.g., `FEngine`, `FRenderer`).

### Rendering Pipeline

Filament uses **clustered forward rendering** with a **frame graph** (`filament/src/fg/`):
1. Frustum culling
2. RenderPass sorts commands by material/depth (`RenderPass.h`)
3. Froxelizer builds per-froxel light lists (`Froxelizer.h`)
4. Depth prepass → Color/lighting pass → Transparent pass
5. Post-processing chain (bloom, DOF, tonemapping, TAA, SSAO, SSR, fog)

PostProcessManager (`filament/src/PostProcessManager.cpp`) orchestrates all post-processing passes via the frame graph.

### Backend Abstraction (`filament/backend/`)

Graphics APIs are abstracted through `DriverApi`:
- Commands are serialized into a `CommandStream` (API-agnostic)
- Driver implementations: `backend/src/opengl/`, `backend/src/vulkan/`, `backend/src/metal/`, `backend/src/webgpu/`
- GPU resources use type-safe `Handle<>` templates

### Material Pipeline

```
.mat source → matc (tools/matc/) → binary package → Material::Builder().package().build()
```

- **filamat** (`libs/filamat/`) — material compiler library; `MaterialBuilder` is the programmatic API
- **filabridge** (`libs/filabridge/`) — shared enums/structs between engine and compiler
- **filaflat** (`libs/filaflat/`) — binary serialization for material packages
- matc compiles GLSL → SPIR-V → backend-specific shaders (MSL, ESSL, GLSL, WGSL), embedding all variants in one package

### Shaders (`shaders/src/`)

Modular GLSL files composed during material compilation:
- `surface_main.vs/fs` — vertex/fragment entry points
- `surface_shading_lit.fs` — lit surface BRDF
- `surface_brdf.fs` — Cook-Torrance, Lambertian
- `surface_shadowing.fs` — shadow sampling
- `post_process_*.fs/vs` — post-processing shaders
- `shading_model_subsurface.fs` — subsurface scattering model

### Entity-Component System

Managers for renderable components:
- **TransformManager** — world transforms (hierarchy)
- **RenderableManager** — mesh + material bindings
- **LightManager** — directional, point, spot lights
- **CameraManager** — camera parameters

### Supporting Libraries (`libs/`)

- **utils** — threading, allocators, data structures (no STL containers in core)
- **math** — vector/matrix types with SIMD
- **gltfio** — glTF 2.0 loader
- **ibl** — image-based lighting preprocessing
- **geometry** — mesh utilities

## Code Style

Follows Android-derived style (see `CODE_STYLE.md`). Key rules:
- 4-space indent, 8-space continuation, 100-column limit
- `{` on same line, camelCase everywhere except UPPER_CASE constants
- Member prefix: `m` (private/protected), `s` (static), `g` (global)
- **No `std::string`** in core engine — use `utils::CString` or `std::string_view`
- **No `<iostream>`** anywhere
- STL in public headers limited to: `array`, `initializer_list`, `iterator`, `limits`, `optional`, `type_traits`, `utility`, `variant`
- `#include <foo/Bar.h>` for public headers, `#include "Private.h"` for private
- Always include the class's own header first in `.cpp` files
- Apache 2.0 copyright header on every file

## SSS Burley Normalized Pipeline (Current Work)

The `burley-normalized` branch implements screen-space Burley Normalized Diffusion (Disney SSS). Key touchpoints:
- **Material system**: `SHADINGMODEL_SUBSURFACE_BURLEY` shading model, per-material `subsurfaceColor` + `scatteringDistance` + `radiusScale` written to 4 MRT buffers (sssDiffuse, sssNormal, sssParams, sssAlbedo — all RGBA16F)
- **Post-process**: single-pass 2D disc blur in `PostProcessManager.cpp` using importance-sampled Burley kernel, bilateral depth/normal rejection, opacity-weighted recombine
- **View options**: `SubsurfaceScatteringOptions` carries `enabled`, `sampleCount`, `scatteringDistance`, `subsurfaceColor`, `worldUnitScale`, `falloffColor`, `temporalNoise`, `fastSampleNormals`
- **Sample app**: `samples/sample_sss_burley.cpp` with ImGui controls, presets, gap analysis table, comparison workflow, performance benchmark
- **ImGui caveat**: the ImGui callback receives the **UI overlay view**, not the main rendering view. Store the main view from setup and use it for rendering options.
- Reference: Unreal Engine SSS implementation at `../UnrealEngine`

### Known Missing Features (TODO)
1. **Temporal accumulation history** — R2 per-pixel jitter exists but no dedicated history buffer; relies on downstream TAA.
2. **Boundary color bleed** — Not implemented. Color can leak at SSS/non-SSS boundaries.
3. **Transmission** — Thickness stored in MRT but not consumed by blur. No back-scattering through thin objects.
4. **Multi-profile support** — Kernel shape (falloffColor) is view-global; per-pixel tint/distance vary via MRT but all share one profile.
5. **Half-res blur path** — No performance fallback for mobile / low-end.
