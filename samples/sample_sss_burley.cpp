/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/arguments.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Options.h>
#include <filament/Renderer.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/View.h>

#include <filagui/ImGuiExtensions.h>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/scalar.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>
#include <utils/getopt.h>

#include <filameshio/MeshReader.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <image/ColorTransform.h>
#include <image/LinearImage.h>
#include <imageio/ImageEncoder.h>

#include <stb_image.h>

#include "generated/resources/resources.h"
#include "generated/resources/monkey.h"

using namespace filament;
using namespace filament::math;
using namespace filamesh;
using namespace image;
using namespace utils;

namespace {

constexpr float PI_F = 3.14159265358979323846f;

enum class DebugView : int {
    FINAL = 0,
    SSS_MEMBERSHIP,
    SSS_INFLUENCE,
    PRE_BLUR_DIFFUSE,
    POST_BLUR_DIFFUSE,
    TERMINATOR_WINDOW,
    BAND_MASK,
    TRANSMISSION,
    COUNT
};

constexpr std::array<const char*, size_t(DebugView::COUNT)> DEBUG_VIEW_NAMES = {{
    "Final",
    "SSS Membership",
    "SSS Influence",
    "Pre-Blur Diffuse",
    "Post-Blur Diffuse",
    "Terminator Window",
    "Band Mask",
    "Transmission"
}};

struct BurleyPreset {
    const char* name;
    const char* slug;
    float3 baseColor;
    float roughness;
    float metallic;
    float reflectance;
    float thickness;
    float3 meanFreePathColor;
    float meanFreePathDistance;
    float worldUnitScale;
    float3 tint;
    float3 boundaryColorBleed;
    float extinctionScale;
    float normalScale;
    float scatteringDistribution;
    float ior;
    float roughness0;
    float roughness1;
    float lobeMix;
    float3 transmissionTintColor;
};

constexpr std::array<BurleyPreset, 4> PRESETS = {{
    {
        "Unreal Burley Reference",
        "unreal_burley_reference",
        { 0.21875f, 0.178259f, 0.13434f },
        0.5f,
        0.0f,
        0.5f,
        0.5f,
        { 1.0f, 0.530583f, 0.526042f },
        0.15f,
        1.0f,
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        1.0f,
        0.08f,
        0.93f,
        1.55f,
        0.75f,
        1.3f,
        0.85f,
        { 1.0f, 1.0f, 1.0f }
    },
    {
        "Skin",
        "skin",
        { 0.8f, 0.5f, 0.4f },
        0.5f,
        0.0f,
        0.5f,
        0.5f,
        { 0.8f, 0.2f, 0.1f },
        0.08f,
        0.1f,
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        1.0f,
        0.08f,
        0.93f,
        1.4f,
        0.75f,
        1.2f,
        0.85f,
        { 1.0f, 1.0f, 1.0f }
    },
    {
        "Marble",
        "marble",
        { 0.9f, 0.9f, 0.85f },
        0.3f,
        0.0f,
        0.5f,
        0.7f,
        { 0.9f, 0.85f, 0.7f },
        0.5f,
        1.0f,
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        1.0f,
        0.08f,
        0.93f,
        1.0f,
        0.75f,
        1.2f,
        0.85f,
        { 1.0f, 1.0f, 1.0f }
    },
    {
        "Wax",
        "wax",
        { 0.9f, 0.8f, 0.6f },
        0.6f,
        0.0f,
        0.5f,
        0.3f,
        { 0.9f, 0.6f, 0.3f },
        0.2f,
        0.1f,
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        1.0f,
        0.08f,
        0.93f,
        1.4f,
        0.75f,
        1.2f,
        0.85f,
        { 1.0f, 1.0f, 1.0f }
    }
}};

constexpr int DEFAULT_PRESET_INDEX = 2;

struct ComparisonViewpoint {
    const char* name;
    const char* slug;
    float yawDegrees;
    float pitchDegrees;
};

constexpr std::array<ComparisonViewpoint, 4> COMPARISON_VIEWPOINTS = {{
    { "Front Lit", "front_lit", 0.0f, 0.0f },
    { "Three Quarter", "three_quarter", 35.0f, -8.0f },
    { "Grazing", "grazing", 75.0f, -5.0f },
    { "Thin Region", "thin_region", 110.0f, -2.0f }
}};

struct ComparisonArtifact {
    DebugView debugView;
    const char* name;
    const char* slug;
};

constexpr std::array<ComparisonArtifact, 8> COMPARISON_ARTIFACTS = {{
    { DebugView::FINAL, "Final", "final" },
    { DebugView::SSS_INFLUENCE, "SSS Influence", "sss_influence" },
    { DebugView::SSS_MEMBERSHIP, "SSS Membership", "sss_membership" },
    { DebugView::PRE_BLUR_DIFFUSE, "Pre-Blur Diffuse", "pre_blur_diffuse" },
    { DebugView::POST_BLUR_DIFFUSE, "Post-Blur Diffuse", "post_blur_diffuse" },
    { DebugView::TERMINATOR_WINDOW, "Terminator Window", "terminator_window" },
    { DebugView::BAND_MASK, "Band Mask", "band_mask" },
    { DebugView::TRANSMISSION, "Transmission", "transmission" }
}};

struct GapRow {
    const char* behavior;
    const char* unrealReference;
    const char* currentFilament;
    const char* visibleSymptom;
    const char* rootCause;
    const char* fixPlan;
};

constexpr std::array<GapRow, 16> GAP_ROWS = {{
    {
        "diffuse/specular separation",
        "Diffuse lighting is filtered, specular is preserved and recombined later.",
        "Implemented via auxiliary diffuse MRT and unblurred specular recombine.",
        "Specular highlights no longer smear with the SSS blur.",
        "Needed explicit diffuse capture in the color pass.",
        "Keep validating against multi-light cases and dual-spec materials."
    },
    {
        "pre-blur setup buffer contents",
        "Setup stores Burley-ready diffuse plus profile metadata.",
        "Diffuse.rgb plus membership alpha is stored alongside per-pixel Burley tint/radius and thickness context.",
        "Blur now reads real per-pixel Burley params instead of one shared view-global profile.",
        "Broad interior scatter can be driven from material data rather than a contour seed alone; profile ids and boundary bleed are still not part of the payload.",
        "Only add profile id / bleed if mixed-profile scenes actually require it."
    },
    {
        "per-pixel SSS membership",
        "Per-pixel profile membership is carried through the setup pass.",
        "Implemented as per-pixel membership alpha in the SSS diffuse buffer.",
        "Membership debug view matches eligible SSS pixels.",
        "Single scalar membership bit is available, but no profile differentiation yet.",
        "Promote membership to profile-aware ids for multi-material scenes."
    },
    {
        "per-pixel scattering parameters",
        "Mean free path and tint are resolved per pixel via the subsurface profile system.",
        "Implemented via per-pixel auxiliary Burley params, with View::SubsurfaceScatteringOptions acting as a multiplier/default.",
        "Different Burley materials can now carry different radius and tint values through the same blur pass, though profile ids are still absent if they overlap in screen space.",
        "Current scope targets Filament-first material parity rather than full Unreal profile assets.",
        "Validate mixed-material scenes before adding profile-id rejection."
    },
    {
        "world-unit kernel scaling",
        "Kernel radius derives from mean free path distance and world unit scale.",
        "Projected radius is still screen-space, but now scaled by per-pixel scattering distance before the view-global multiplier.",
        "Sample presets map more directly into the engine path, though no explicit world-unit field is stored in the SSS payload and scene-scale mismatch can still drift if radius authoring is inconsistent.",
        "World unit scale is still folded into material/sample setup rather than stored independently.",
        "Only add an explicit world-unit field if real content shows scale inconsistency."
    },
    {
        "Burley kernel shape",
        "Burley uses normalized diffusion with profile-driven adaptive sampling.",
        "A single-radius separable Burley kernel is used.",
        "Band shape is plausible but not yet profile-accurate under all presets.",
        "Current kernel is scalar and global rather than profile-driven.",
        "Match Unreal's per-profile scaling before tuning sample count or weights."
    },
    {
        "bilateral depth rejection",
        "Depth-aware rejection prevents leaking across discontinuities.",
        "Implemented in the blur pass.",
        "Geometric borders stay sharper than the first prototype.",
        "Depth threshold is still tied to the global scattering distance.",
        "Parameterize the rejection threshold per pixel once profile radii are stored."
    },
    {
        "bilateral normal rejection",
        "Normals are used to reject taps across sharp shading changes.",
        "Implemented with the stored shaded normal buffer.",
        "SSS weighting now follows the same normal-map detail as the material shading path.",
        "A dedicated SSS normal target is carried from the color pass into blur and recombine.",
        "Validate the stored-normal path on thin detail and grazing angles."
    },
    {
        "recombine formula",
        "Burley adds scattered diffuse back to untouched non-SSS lighting.",
        "Implemented as original diffuse plus positive scattering delta tinted by the per-pixel Burley params, with a separate thin-region transmission lift.",
        "Final shading reads less like a rim blur and more like broad interior diffusion plus backlit translucency, fixing the earlier contour-specific gating problem.",
        "Transmission is still approximated from thickness and geometry rather than a full profile model.",
        "Tune transmission strength and mixed-material behavior against captures before adding more profile complexity."
    },
    {
        "base color application point",
        "Surface albedo participates at setup/recombine according to the profile.",
        "Base color currently still follows Filament's material path rather than an Unreal-equivalent profile block.",
        "Reference albedo is close, but profile coupling is incomplete.",
        "Surface albedo is not stored separately in the SSS setup data.",
        "Add explicit Burley profile mapping for surface albedo versus tint."
    },
    {
        "boundary color bleed",
        "Boundary bleed is profile-aware and configurable.",
        "Not implemented.",
        "Different SSS materials would hard-stop or incorrectly mix.",
        "No profile id or boundary tuning reaches the blur pass today.",
        "Add profile-aware bleed weighting once profile ids exist."
    },
    {
        "transmission / thickness lighting",
        "Transmission is handled separately from lateral surface blur.",
        "Implemented as a distinct recombine-stage backlight / silhouette lift driven by thickness, geometry, and Burley tint.",
        "Ears and other thin regions can now pick up a separate translucent glow from the main blur lobe, though it is still an approximation rather than full Unreal transmission parity.",
        "No dedicated thickness texture or full profile transmission block exists yet.",
        "Tune against thin-region captures before deciding whether a richer transmission payload is necessary."
    },
    {
        "multi-material profile handling",
        "Multiple subsurface profiles can coexist in one scene.",
        "Not implemented.",
        "Different materials would share one global blur profile.",
        "No profile id cache or per-pixel profile selection exists.",
        "Store profile ids and reject or bleed taps based on profile compatibility."
    },
    {
        "dual-spec preservation",
        "Dual specular is a separate profile feature layered after SSS.",
        "Not implemented.",
        "Skin-like glints will still differ from Unreal even when the band matches.",
        "Current work isolates diffuse scattering from the specular path only.",
        "Keep it isolated until diffuse Burley parity is acceptable."
    },
    {
        "half-res / full-res behavior",
        "Unreal can switch to separable or half-res modes based on quality settings.",
        "Only the full-resolution path is present.",
        "Performance parity is not representative yet.",
        "No half-res or AFIS fallback has been added.",
        "Add after full-resolution parity is stable."
    },
    {
        "TAA interaction",
        "Burley is evaluated with TAA-aware quality modes.",
        "Comparison workflow disables TAA for baseline captures.",
        "Temporal behavior is intentionally out of scope for the baseline report.",
        "Need stable direct-light captures before testing temporal accumulation.",
        "Re-enable TAA only after the direct-light capture set matches well."
    }
}};

struct CaptureTask {
    DebugView debugView;
    const char* artifactName;
    const char* artifactSlug;
};

struct App {
    Config config;
    Entity light;
    Material* material = nullptr;
    MaterialInstance* materialInstance = nullptr;
    Texture* normalMap = nullptr;
    MeshReader::Mesh mesh;
    mat4f transform;
    mat4f baseMeshTransform;

    float3 baseColor = { 0.8f, 0.5f, 0.4f };
    float roughness = 0.5f;
    float metallic = 0.0f;
    float reflectance = 0.5f;
    float thickness = 0.5f;
    float scatteringDistance = 0.15f;
    float3 subsurfaceColor = { 0.8f, 0.2f, 0.1f };
    float4 emissive = { 0.0f, 0.0f, 0.0f, 0.0f };

    float3 meanFreePathColor = { 1.0f, 0.530583f, 0.526042f };
    float meanFreePathDistance = 0.15f;
    float worldUnitScale = 1.0f;
    float3 tint = { 1.0f, 1.0f, 1.0f };
    float3 boundaryColorBleed = { 1.0f, 1.0f, 1.0f };
    float extinctionScale = 1.0f;
    float normalScale = 0.08f;
    float scatteringDistribution = 0.93f;
    float ior = 1.55f;
    float roughness0 = 0.75f;
    float roughness1 = 1.3f;
    float lobeMix = 0.85f;
    float3 transmissionTintColor = { 1.0f, 1.0f, 1.0f };

    bool sssEnabled = true;
    int sssSampleCount = 11;
    DebugView debugView = DebugView::FINAL;

    bool screenshotRequested = false;
    bool screenshotCaptureArmed = false;
    bool comparisonCaptureRequested = false;
    bool comparisonCaptureActive = false;
    size_t comparisonCaptureIndex = 0;
    std::vector<CaptureTask> comparisonCaptureTasks;
    std::string comparisonOutputDir;
    std::string gitCommit;

    int presetIndex = DEFAULT_PRESET_INDEX;
    int viewpointIndex = 0;
    int restoreViewpointIndex = 0;
    DebugView restoreDebugView = DebugView::FINAL;
    float restoreIblIntensity = 30000.0f;

    View* mainView = nullptr;

    float lightIntensity = 110000.0f;
    float3 lightDirection = { 0.7f, -1.0f, -0.8f };
    float3 lightColor = { 0.98f, 0.92f, 0.89f };
    float sunAngularRadius = 1.9f;
    float iblIntensity = 0.0f;
    Scene* scene = nullptr;
};

static const char* IBL_FOLDER = "assets/ibl/lightroom_14b";

float degreesToRadians(float degrees) {
    return degrees * PI_F / 180.0f;
}

std::string quoteJson(std::string const& value) {
    std::ostringstream out;
    out << '"';
    for (char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            default: out << c; break;
        }
    }
    out << '"';
    return out.str();
}

std::string vectorToJson(float3 const& value) {
    std::ostringstream out;
    out << "[" << value.x << ", " << value.y << ", " << value.z << "]";
    return out.str();
}

std::string matrixToJson(mat4 const& value) {
    std::ostringstream out;
    out << "[";
    for (size_t row = 0; row < 4; row++) {
        if (row > 0) {
            out << ", ";
        }
        out << "[" << value[row][0] << ", " << value[row][1] << ", " << value[row][2]
            << ", " << value[row][3] << "]";
    }
    out << "]";
    return out.str();
}

std::string getGitCommit() {
    std::string commit = "unknown";
    FILE* pipe = popen("git rev-parse --short HEAD 2>/dev/null", "r");
    if (!pipe) {
        return commit;
    }
    char buffer[64] = {};
    if (fgets(buffer, sizeof(buffer), pipe)) {
        commit = buffer;
        while (!commit.empty() && (commit.back() == '\n' || commit.back() == '\r')) {
            commit.pop_back();
        }
    }
    pclose(pipe);
    return commit;
}

Texture* loadNormalMap(Engine* engine, const uint8_t* normals, size_t nbytes) {
    int width, height, channels;
    unsigned char* data = stbi_load_from_memory(normals, nbytes, &width, &height, &channels, 3);
    if (!data) {
        return nullptr;
    }

    Texture* normalMap = Texture::Builder()
            .width(uint32_t(width))
            .height(uint32_t(height))
            .levels(0xff)
            .format(Texture::InternalFormat::RGB8)
            .usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE)
            .build(*engine);
    Texture::PixelBufferDescriptor buffer(data, size_t(width * height * 3),
            Texture::Format::RGB, Texture::Type::UBYTE,
            (Texture::PixelBufferDescriptor::Callback) &stbi_image_free);
    normalMap->setImage(*engine, 0, std::move(buffer));
    normalMap->generateMipmaps(*engine);
    return normalMap;
}

SubsurfaceScatteringDebugMode toSssDebugMode(DebugView view) {
    switch (view) {
        case DebugView::SSS_MEMBERSHIP:
            return SubsurfaceScatteringDebugMode::MEMBERSHIP;
        case DebugView::SSS_INFLUENCE:
            return SubsurfaceScatteringDebugMode::INFLUENCE;
        case DebugView::PRE_BLUR_DIFFUSE:
            return SubsurfaceScatteringDebugMode::PRE_BLUR_DIFFUSE;
        case DebugView::POST_BLUR_DIFFUSE:
            return SubsurfaceScatteringDebugMode::POST_BLUR_DIFFUSE;
        case DebugView::TERMINATOR_WINDOW:
            return SubsurfaceScatteringDebugMode::TERMINATOR_WINDOW;
        case DebugView::BAND_MASK:
            return SubsurfaceScatteringDebugMode::BAND_MASK;
        case DebugView::TRANSMISSION:
            return SubsurfaceScatteringDebugMode::TRANSMISSION;
        default:
            return SubsurfaceScatteringDebugMode::NONE;
    }
}

void applyPreset(App& app, BurleyPreset const& preset) {
    app.baseColor = preset.baseColor;
    app.roughness = preset.roughness;
    app.metallic = preset.metallic;
    app.reflectance = preset.reflectance;
    app.thickness = preset.thickness;
    app.meanFreePathColor = preset.meanFreePathColor;
    app.meanFreePathDistance = preset.meanFreePathDistance;
    app.worldUnitScale = preset.worldUnitScale;
    app.subsurfaceColor = preset.meanFreePathColor;
    app.scatteringDistance = preset.meanFreePathDistance * preset.worldUnitScale;
    app.tint = preset.tint;
    app.boundaryColorBleed = preset.boundaryColorBleed;
    app.extinctionScale = preset.extinctionScale;
    app.normalScale = preset.normalScale;
    app.scatteringDistribution = preset.scatteringDistribution;
    app.ior = preset.ior;
    app.roughness0 = preset.roughness0;
    app.roughness1 = preset.roughness1;
    app.lobeMix = preset.lobeMix;
    app.transmissionTintColor = preset.transmissionTintColor;
}

void writeComparisonMetadata(App const& app, View const* view) {
    std::filesystem::create_directories(app.comparisonOutputDir);

    Camera const& camera = view->getCamera();
    Viewport const viewport = view->getViewport();
    auto const& preset = PRESETS[app.presetIndex];
    auto const& viewpoint = COMPARISON_VIEWPOINTS[size_t(app.viewpointIndex)];

    std::ofstream out(app.comparisonOutputDir + "/metadata.json", std::ios::trunc);
    out << "{\n";
    out << "  \"engineCommit\": " << quoteJson(app.gitCommit) << ",\n";
    out << "  \"preset\": " << quoteJson(preset.name) << ",\n";
    out << "  \"presetSlug\": " << quoteJson(preset.slug) << ",\n";
    out << "  \"viewpoint\": " << quoteJson(viewpoint.name) << ",\n";
    out << "  \"viewport\": {\"width\": " << viewport.width << ", \"height\": " << viewport.height
        << "},\n";
    out << "  \"cameraModelMatrix\": " << matrixToJson(camera.getModelMatrix()) << ",\n";
    out << "  \"cameraProjectionMatrix\": " << matrixToJson(camera.getProjectionMatrix()) << ",\n";
    out << "  \"lightDirection\": " << vectorToJson(app.lightDirection) << ",\n";
    out << "  \"lightColor\": " << vectorToJson(app.lightColor) << ",\n";
    out << "  \"lightIntensity\": " << app.lightIntensity << ",\n";
    out << "  \"iblIntensity\": " << app.iblIntensity << ",\n";
    out << "  \"sampleCount\": " << app.sssSampleCount << ",\n";
    out << "  \"baseColor\": " << vectorToJson(app.baseColor) << ",\n";
    out << "  \"thickness\": " << app.thickness << ",\n";
    out << "  \"scatteringDistance\": " << app.scatteringDistance << ",\n";
    out << "  \"subsurfaceColor\": " << vectorToJson(app.subsurfaceColor) << ",\n";
    out << "  \"surfaceAlbedo\": " << vectorToJson(preset.baseColor) << ",\n";
    out << "  \"meanFreePathColor\": " << vectorToJson(app.meanFreePathColor) << ",\n";
    out << "  \"meanFreePathDistance\": " << app.meanFreePathDistance << ",\n";
    out << "  \"worldUnitScale\": " << app.worldUnitScale << ",\n";
    out << "  \"viewScatteringDistanceMultiplier\": 1.0,\n";
    out << "  \"viewSubsurfaceColorMultiplier\": " << vectorToJson(float3{1.0f, 1.0f, 1.0f}) << ",\n";
    out << "  \"tint\": " << vectorToJson(app.tint) << ",\n";
    out << "  \"boundaryColorBleed\": " << vectorToJson(app.boundaryColorBleed) << ",\n";
    out << "  \"extinctionScale\": " << app.extinctionScale << ",\n";
    out << "  \"normalScale\": " << app.normalScale << ",\n";
    out << "  \"scatteringDistribution\": " << app.scatteringDistribution << ",\n";
    out << "  \"ior\": " << app.ior << ",\n";
    out << "  \"roughness0\": " << app.roughness0 << ",\n";
    out << "  \"roughness1\": " << app.roughness1 << ",\n";
    out << "  \"lobeMix\": " << app.lobeMix << ",\n";
    out << "  \"transmissionTintColor\": " << vectorToJson(app.transmissionTintColor) << ",\n";
    out << "  \"thinRegionTransmission\": true\n";
    out << "}\n";
}

void writeComparisonReport(App const& app) {
    std::ofstream out(app.comparisonOutputDir + "/comparison_report.md", std::ios::trunc);
    out << "# Burley SSS Comparison Report\n\n";
    out << "This capture set is the Filament side of the repeatable Unreal-vs-Filament Burley "
           "comparison workflow.\n\n";
    out << "- Engine commit: `" << app.gitCommit << "`\n";
    out << "- Preset: `" << PRESETS[app.presetIndex].name << "`\n";
    out << "- Metadata: [`metadata.json`](./metadata.json)\n";
    out << "- Baseline render settings: direct light only, fixed viewpoints, TAA disabled, bloom "
           "disabled, DOF disabled, SSR disabled\n\n";

    out << "## Parameter Mapping\n\n";
    out << "| Unreal Burley term | Filament current mapping | Status |\n";
    out << "| --- | --- | --- |\n";
    out << "| Surface Albedo | stored in `sssAlbedo` for lighting-space recombine | Implemented |\n";
    out << "| Mean Free Path Color | material `subsurfaceColor` authored per pixel | Approximate |\n";
    out << "| Mean Free Path Distance | material `scatteringDistance` authored per pixel | Approximate |\n";
    out << "| World Unit Scale | view-level Burley radius calibration | Implemented |\n";
    out << "| Tint | fixed white in current sample flow | Not an active parity lever |\n";
    out << "| Boundary Color Bleed | fixed white in current sample flow; not consumed by engine | Missing in engine |\n";
    out << "| Transmission Tint Color | fixed white in current sample flow | Not an active parity lever |\n";
    out << "| Transmission block | separate thin-region transmission lift driven by thickness, IOR, and current Burley scale | Approximate |\n";
    out << "| Dual Specular | untouched default lit specular path | Missing |\n\n";

    out << "## Artifact Grid\n\n";
    out << "Capture filenames are deterministic and overwrite previous runs for the same preset.\n\n";
    auto const& viewpoint = COMPARISON_VIEWPOINTS[size_t(app.viewpointIndex)];
    out << "Current viewpoint: `" << viewpoint.slug << "`\n\n";
    for (auto const& artifact : COMPARISON_ARTIFACTS) {
        out << "- `" << artifact.slug << ".png`\n";
    }
    out << "\n";

    out << "## Gap Matrix\n\n";
    out << "| Behavior | Unreal reference | Current Filament | Visible symptom | Root cause guess | Fix required |\n";
    out << "| --- | --- | --- | --- | --- | --- |\n";
    for (auto const& row : GAP_ROWS) {
        out << "| " << row.behavior << " | " << row.unrealReference << " | "
            << row.currentFilament << " | " << row.visibleSymptom << " | "
            << row.rootCause << " | " << row.fixPlan << " |\n";
    }
    out << "\n## Next Action\n\n";
    out << "Tighten the Burley scale and `LerpFactor` calibration against the Unreal Burley "
           "profile screenshot before adding profile ids or dual-spec complexity.\n";
}

void applyViewpoint(App& app) {
    auto const& viewpoint = COMPARISON_VIEWPOINTS[size_t(app.viewpointIndex)];
    mat3f const rotation = mat3f::eulerYXZ(
            degreesToRadians(viewpoint.yawDegrees),
            degreesToRadians(viewpoint.pitchDegrees), 0.0f);
    app.transform = mat4f{ rotation, float3(0.0f, 0.0f, -4.0f) } * app.baseMeshTransform;
}

void applyMaterialDebugView(App& app) {
    auto* mi = app.materialInstance;
    if (!mi) {
        return;
    }

    mi->setParameter("baseColor", app.baseColor);
    mi->setParameter("roughness", app.roughness);
    mi->setParameter("metallic", app.metallic);
    mi->setParameter("reflectance", app.reflectance);
    mi->setParameter("thickness", app.thickness);
    mi->setParameter("scatteringDistance", app.scatteringDistance);
    mi->setParameter("subsurfaceColor", app.subsurfaceColor);
    mi->setParameter("emissive", app.emissive);
}

void applyViewOptions(App& app) {
    if (!app.mainView) {
        return;
    }

    SubsurfaceScatteringOptions sssOptions;
    sssOptions.enabled = app.sssEnabled;
    sssOptions.sampleCount = uint8_t(app.sssSampleCount);
    sssOptions.scatteringDistance = 1.0f;
    sssOptions.subsurfaceColor = float3{ 1.0f, 1.0f, 1.0f };
    sssOptions.worldUnitScale = app.worldUnitScale;
    sssOptions.ior = app.ior;
    sssOptions.debugMode = toSssDebugMode(app.debugView);
    app.mainView->setSubsurfaceScatteringOptions(sssOptions);
}

void applyLighting(App& app, Engine* engine) {
    if (!engine) {
        return;
    }

    auto& lm = engine->getLightManager();
    auto li = lm.getInstance(app.light);
    lm.setColor(li, Color::toLinear<ACCURATE>(
            sRGBColor(app.lightColor.x, app.lightColor.y, app.lightColor.z)));
    lm.setIntensity(li, app.lightIntensity);
    lm.setDirection(li, normalize(app.lightDirection));
    lm.setSunAngularRadius(li, app.sunAngularRadius);

    auto* ibl = app.scene ? app.scene->getIndirectLight() : nullptr;
    if (ibl) {
        ibl->setIntensity(app.iblIntensity);
    }
}

void syncSceneState(App& app, Engine* engine) {
    applyViewpoint(app);
    applyMaterialDebugView(app);
    applyViewOptions(app);
    applyLighting(app, engine);

    auto& tcm = engine->getTransformManager();
    auto ti = tcm.getInstance(app.mesh.renderable);
    tcm.setTransform(ti, app.transform);
}

void configureComparisonView(View* view) {
    view->setAntiAliasing(View::AntiAliasing::NONE);

    auto taa = view->getTemporalAntiAliasingOptions();
    taa.enabled = false;
    view->setTemporalAntiAliasingOptions(taa);

    BloomOptions bloom = view->getBloomOptions();
    bloom.enabled = false;
    view->setBloomOptions(bloom);

    DepthOfFieldOptions dof = view->getDepthOfFieldOptions();
    dof.enabled = false;
    view->setDepthOfFieldOptions(dof);

    ScreenSpaceReflectionsOptions ssr;
    ssr.enabled = false;
    view->setScreenSpaceReflectionsOptions(ssr);
}

void queueComparisonCapture(App& app) {
    app.comparisonCaptureTasks.clear();
    for (auto const& artifact : COMPARISON_ARTIFACTS) {
        app.comparisonCaptureTasks.push_back({
            artifact.debugView,
            artifact.name,
            artifact.slug
        });
    }
}

void beginComparisonCapture(App& app) {
    app.restoreDebugView = app.debugView;
    app.restoreViewpointIndex = app.viewpointIndex;
    app.restoreIblIntensity = app.iblIntensity;

    app.sssEnabled = true;
    app.iblIntensity = 0.0f;
    app.comparisonOutputDir = "captures/sss_burley/" + std::string(PRESETS[app.presetIndex].slug);
    std::filesystem::create_directories(app.comparisonOutputDir);

    queueComparisonCapture(app);
    app.comparisonCaptureIndex = 0;
    app.comparisonCaptureActive = !app.comparisonCaptureTasks.empty();
    if (app.comparisonCaptureActive) {
        app.debugView = app.comparisonCaptureTasks.front().debugView;
        writeComparisonMetadata(app, app.mainView);
        writeComparisonReport(app);
        std::cout << "Starting Burley comparison capture in: " << app.comparisonOutputDir
                  << std::endl;
    }
}

void finishComparisonCapture(App& app) {
    app.comparisonCaptureActive = false;
    app.debugView = app.restoreDebugView;
    app.viewpointIndex = app.restoreViewpointIndex;
    app.iblIntensity = app.restoreIblIntensity;
    std::cout << "Burley comparison capture complete. Artifacts saved to: "
              << app.comparisonOutputDir << std::endl;
}

void printUsage(char* name) {
    std::string execName(Path(name).getName());
    std::string usage(
        "SAMPLE_SSS_BURLEY demonstrates Burley normalized diffusion SSS\n"
        "Usage:\n"
        "    SAMPLE_SSS_BURLEY [options]\n"
        "Options:\n"
        "   --help, -h\n"
        "       Prints this message\n\n"
        "API_USAGE"
    );
    const std::string from("SAMPLE_SSS_BURLEY");
    for (size_t pos = usage.find(from); pos != std::string::npos; pos = usage.find(from, pos)) {
        usage.replace(pos, from.length(), execName);
    }
    const std::string apiUsage("API_USAGE");
    for (size_t pos = usage.find(apiUsage); pos != std::string::npos;
            pos = usage.find(apiUsage, pos)) {
        usage.replace(pos, apiUsage.length(), samples::getBackendAPIArgumentsUsage());
    }
    std::cout << usage;
}

int handleCommandLineArguments(int argc, char* argv[], App* app) {
    static constexpr const char* OPTSTR = "ha:";
    static const utils::getopt::option OPTIONS[] = {
        { "help", utils::getopt::no_argument,       nullptr, 'h' },
        { "api",  utils::getopt::required_argument, nullptr, 'a' },
        { nullptr, 0, nullptr, 0 }
    };
    int opt;
    int optionIndex = 0;
    while ((opt = utils::getopt::getopt_long(argc, argv, OPTSTR, OPTIONS, &optionIndex)) >= 0) {
        std::string arg(utils::getopt::optarg ? utils::getopt::optarg : "");
        switch (opt) {
            default:
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'a':
                app->config.backend = samples::parseArgumentsForBackend(arg);
                break;
        }
    }
    return utils::getopt::optind;
}

} // namespace

int main(int argc, char** argv) {
    App app;
    app.config.title = "SSS Burley Normalized Diffusion";
    app.config.iblDirectory = FilamentApp::getRootAssetsPath() + IBL_FOLDER;
    app.gitCommit = getGitCommit();
    handleCommandLineArguments(argc, argv, &app);
    applyPreset(app, PRESETS[app.presetIndex]);

    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.mainView = view;
        app.scene = scene;

        configureComparisonView(view);

        auto& tcm = engine->getTransformManager();
        auto& rcm = engine->getRenderableManager();
        auto& em = EntityManager::get();

        app.material = Material::Builder()
            .package(RESOURCES_SANDBOXSUBSURFACEBURLEY_DATA, RESOURCES_SANDBOXSUBSURFACEBURLEY_SIZE)
            .build(*engine);

        auto* mi = app.materialInstance = app.material->createInstance();
        app.normalMap = loadNormalMap(engine, MONKEY_NORMAL_DATA, MONKEY_NORMAL_SIZE);
        if (app.normalMap) {
            TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                    TextureSampler::MagFilter::LINEAR);
            mi->setParameter("normalMap", app.normalMap, sampler);
        }
        app.mesh = MeshReader::loadMeshFromBuffer(engine, MONKEY_SUZANNE_DATA, nullptr, nullptr, mi);
        auto ti = tcm.getInstance(app.mesh.renderable);
        app.baseMeshTransform = tcm.getWorldTransform(ti);
        applyViewpoint(app);

        rcm.setCastShadows(rcm.getInstance(app.mesh.renderable), false);
        scene->addEntity(app.mesh.renderable);

        app.light = em.create();
        LightManager::Builder(LightManager::Type::SUN)
            .color(Color::toLinear<ACCURATE>(
                    sRGBColor(app.lightColor.x, app.lightColor.y, app.lightColor.z)))
            .intensity(app.lightIntensity)
            .direction(normalize(app.lightDirection))
            .sunAngularRadius(app.sunAngularRadius)
            .castShadows(false)
            .build(*engine, app.light);
        scene->addEntity(app.light);

        syncSceneState(app, engine);
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.light);
        engine->destroy(app.normalMap);
        engine->destroy(app.mesh.renderable);
        engine->destroy(app.materialInstance);
        engine->destroy(app.material);
    };

    FilamentApp::get().animate([&app](Engine* engine, View*, double) {
        if (app.screenshotRequested) {
            app.screenshotRequested = false;
            app.screenshotCaptureArmed = true;
        }
        if (app.comparisonCaptureRequested) {
            app.comparisonCaptureRequested = false;
            beginComparisonCapture(app);
        }
        syncSceneState(app, engine);
    });

    auto imgui = [&app](Engine* engine, View*) {
        if (app.screenshotCaptureArmed || app.comparisonCaptureActive) {
            syncSceneState(app, engine);
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Once);
        ImGui::Begin("SSS Burley Parameters");

        if (ImGui::CollapsingHeader("Canonical Comparison", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (app.presetIndex != DEFAULT_PRESET_INDEX) {
                app.presetIndex = DEFAULT_PRESET_INDEX;
            }
            if (ImGui::Button("Reset to Marble Defaults")) {
                applyPreset(app, PRESETS[DEFAULT_PRESET_INDEX]);
            }
            ImGui::TextUnformatted("Default preset: Marble");

            if (ImGui::Button("Capture Comparison Set")) {
                app.comparisonCaptureRequested = true;
            }
            ImGui::TextWrapped("Outputs overwrite deterministically under %s",
                    app.comparisonOutputDir.empty() ?
                            "captures/sss_burley/<preset>" :
                            app.comparisonOutputDir.c_str());
        }

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Base Color", &app.baseColor.x);
            ImGui::SliderFloat("Roughness", &app.roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &app.metallic, 0.0f, 1.0f);
            ImGui::SliderFloat("Reflectance", &app.reflectance, 0.0f, 1.0f);
        }

        if (ImGui::CollapsingHeader("Subsurface Scattering", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Thickness", &app.thickness, 0.0f, 1.0f);
            ImGui::SliderFloat("Mean Free Path Distance", &app.scatteringDistance, 0.0f, 1.0f,
                    "%.4f");
            ImGui::ColorEdit3("Mean Free Path Color", &app.subsurfaceColor.x);
            ImGui::SliderFloat("World Unit Scale", &app.worldUnitScale, 0.1f, 4.0f, "%.3f");
            ImGui::SliderFloat("IOR", &app.ior, 1.0f, 3.0f);
        }

        if (ImGui::CollapsingHeader("Emissive")) {
            float3 emissiveColor = { app.emissive.x, app.emissive.y, app.emissive.z };
            float emissiveStrength = app.emissive.w;
            ImGui::ColorEdit3("Emissive Color", &emissiveColor.x);
            ImGui::SliderFloat("Emissive Strength", &emissiveStrength, 0.0f, 10.0f);
            app.emissive = {
                emissiveColor.x, emissiveColor.y, emissiveColor.z, emissiveStrength
            };
        }

        if (ImGui::CollapsingHeader("SSS Blur Pass", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable SSS Blur", &app.sssEnabled);
            ImGui::SliderInt("Sample Count", &app.sssSampleCount, 3, 25);
            if ((app.sssSampleCount & 1) == 0) {
                app.sssSampleCount++;
            }
        }

        if (ImGui::CollapsingHeader("Light")) {
            ImGui::ColorEdit3("Light Color", &app.lightColor.x);
            ImGui::SliderFloat("Lux", &app.lightIntensity, 0.0f, 150000.0f);
            ImGui::SliderFloat("Sun Size", &app.sunAngularRadius, 0.1f, 10.0f);
            ImGuiExt::DirectionWidget("Direction", app.lightDirection.v);
            app.lightDirection = normalize(app.lightDirection);
            ImGui::SliderFloat("IBL Intensity", &app.iblIntensity, 0.0f, 50000.0f);
        }

        if (ImGui::CollapsingHeader("Debug Views", ImGuiTreeNodeFlags_DefaultOpen)) {
            int debugMode = int(app.debugView);
            for (int i = 0; i < int(DebugView::COUNT); i++) {
                ImGui::RadioButton(DEBUG_VIEW_NAMES[size_t(i)], &debugMode, i);
            }
            app.debugView = DebugView(debugMode);
        }

        if (ImGui::CollapsingHeader("Capture")) {
            if (ImGui::Button("Take Screenshot (PNG)")) {
                app.screenshotRequested = true;
            }
            ImGui::Text("Single-frame screenshots save to the current directory.");
        }

        ImGui::End();

        syncSceneState(app, engine);
    };

    auto postRender = [&app](Engine*, View* view, Scene*, Renderer* renderer) {
        auto scheduleReadback = [&](std::string path, std::string label) {
            const Viewport& vp = view->getViewport();
            uint8_t* pixels = new uint8_t[vp.width * vp.height * 4];

            struct CaptureState {
                View* view;
                std::string path;
                std::string label;
            };

            backend::PixelBufferDescriptor buffer(pixels, vp.width * vp.height * 4,
                    backend::PixelBufferDescriptor::PixelDataFormat::RGBA,
                    backend::PixelBufferDescriptor::PixelDataType::UBYTE,
                    [](void* buffer, size_t, void* user) {
                        auto* state = static_cast<CaptureState*>(user);
                        const Viewport& viewport = state->view->getViewport();

                        LinearImage image(toLinearWithAlpha<uint8_t>(viewport.width, viewport.height,
                                viewport.width * 4, static_cast<uint8_t*>(buffer)));

                        std::ofstream output(state->path,
                                std::ios::binary | std::ios::trunc);
                        ImageEncoder::encode(output, ImageEncoder::Format::PNG, image, "",
                                state->path);

                        std::cout << "Saved " << state->label << ": " << state->path << std::endl;

                        delete[] static_cast<uint8_t*>(buffer);
                        delete state;
                    },
                    new CaptureState{ view, std::move(path), std::move(label) });

            renderer->readPixels(
                    uint32_t(vp.left), uint32_t(vp.bottom), vp.width, vp.height, std::move(buffer));
        };

        if (app.screenshotCaptureArmed) {
            app.screenshotCaptureArmed = false;
            scheduleReadback("sss_burley_manual.png", "manual screenshot");
        }

        if (app.comparisonCaptureActive && app.comparisonCaptureIndex < app.comparisonCaptureTasks.size()) {
            auto const& task = app.comparisonCaptureTasks[app.comparisonCaptureIndex];
            std::string path = app.comparisonOutputDir + "/" + task.artifactSlug + ".png";
            std::string label = std::string(task.artifactName);
            scheduleReadback(path, label);

            app.comparisonCaptureIndex++;
            if (app.comparisonCaptureIndex < app.comparisonCaptureTasks.size()) {
                auto const& nextTask = app.comparisonCaptureTasks[app.comparisonCaptureIndex];
                app.debugView = nextTask.debugView;
            } else {
                finishComparisonCapture(app);
            }
        }
    };

    FilamentApp::get().run(app.config, setup, cleanup, imgui,
            FilamentApp::PreRenderCallback(), postRender);

    return 0;
}
