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
#include <chrono>
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
    float roughness0;
    float roughness1;
    float lobeMix;
};

// All distance values in centimeters (cm).
// worldUnitScale = cm per scene unit (Suzanne ~2 units wide ≈ 20cm, so 1 unit ≈ 10cm).
constexpr BurleyPreset DEFAULT_PRESET = {
    "Default",
    "default",
    { 1.0f, 1.0f, 1.0f },         // baseColor — pure white for debugging
    0.5f,                           // roughness
    0.0f,                           // metallic
    0.5f,                           // reflectance
    0.5f,                           // thickness
    { 1.0f, 0.530583f, 0.526042f }, // meanFreePathColor (UE reference)
    1.2f,                           // meanFreePathDistance (cm)
    10.0f,                          // worldUnitScale (cm/scene-unit)
    0.75f,                          // roughness0
    1.3f,                           // roughness1
    0.85f                           // lobeMix
};

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
    const char* name;
    const char* slug;
};

constexpr std::array<ComparisonArtifact, 1> COMPARISON_ARTIFACTS = {{
    { "Final", "final" }
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
        "Setup stores Burley-ready diffuse plus per-pixel material metadata.",
        "Diffuse.rgb plus membership alpha is stored alongside per-pixel Burley tint/radius and thickness context.",
        "Blur now reads real per-pixel Burley params instead of one shared view-global preset.",
        "Broad interior scatter can be driven from material data rather than a contour seed alone; boundary bleed is still not part of the payload.",
        "Keep improving direct-authored Burley fidelity before adding more payload complexity."
    },
    {
        "per-pixel SSS membership",
        "Per-pixel SSS membership is carried through the setup pass.",
        "Implemented as per-pixel membership alpha in the SSS diffuse buffer.",
        "Membership debug view matches eligible SSS pixels.",
        "Single scalar membership bit is available for the Burley path.",
        "Keep the membership mask simple unless captures show a real need for more separation."
    },
    {
        "per-pixel scattering parameters",
        "Mean free path and tint are resolved per pixel through the material path.",
        "Implemented via per-pixel auxiliary Burley params, with View::SubsurfaceScatteringOptions acting as a multiplier/default.",
        "Different Burley materials can now carry different radius and tint values through the same blur pass.",
        "Current scope targets Filament-first material parity rather than Unreal asset parity.",
        "Validate mixed-material scenes before adding more rejection complexity."
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
        "Burley uses normalized diffusion with adaptive sampling.",
        "A single-radius separable Burley kernel is used.",
        "Band shape is plausible but not yet fully accurate under all presets.",
        "Current kernel still uses approximations rather than the full Unreal Burley scaling path.",
        "Match Unreal's Burley scaling before tuning sample count or weights."
    },
    {
        "bilateral depth rejection",
        "Depth-aware rejection prevents leaking across discontinuities.",
        "Implemented in the blur pass.",
        "Geometric borders stay sharper than the first prototype.",
        "Depth threshold is still tied to the global scattering distance.",
        "Parameterize the rejection threshold per pixel once Burley radii are stored more faithfully."
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
        "Transmission is still approximated from thickness and geometry rather than a full data model.",
        "Tune transmission strength and behavior against captures before adding more complexity."
    },
    {
        "base color application point",
        "Surface albedo participates at setup/recombine according to the Burley path.",
        "Base color currently still follows Filament's material path rather than an Unreal-equivalent parameter block.",
        "Reference albedo is close, but Burley coupling is incomplete.",
        "Surface albedo is not stored separately in the SSS setup data.",
        "Add more explicit Burley mapping for surface albedo versus tint."
    },
    {
        "boundary color bleed",
        "Boundary bleed is configurable.",
        "Not implemented.",
        "Different SSS materials would hard-stop or incorrectly mix.",
        "No boundary tuning reaches the blur pass today.",
        "Add boundary-aware bleed weighting only if captures justify it."
    },
    {
        "transmission / thickness lighting",
        "Transmission is handled separately from lateral surface blur.",
        "Implemented as a distinct recombine-stage backlight / silhouette lift driven by thickness, geometry, and Burley tint.",
        "Ears and other thin regions can now pick up a separate translucent glow from the main blur lobe, though it is still an approximation rather than full Unreal transmission parity.",
        "No dedicated thickness texture or full transmission block exists yet.",
        "Tune against thin-region captures before deciding whether a richer transmission payload is necessary."
    },
    {
        "multi-material Burley handling",
        "Multiple subsurface materials can coexist in one scene.",
        "Not implemented.",
        "Different materials still share the same postprocess structure.",
        "No extra material-compatibility rejection exists today.",
        "Only add more separation logic if captures show a real problem."
    },
    {
        "dual-spec preservation",
        "Dual specular is a separate Burley feature layered after SSS.",
        "Implemented in the material and lighting path.",
        "Specular highlight shape can now diverge from the single-lobe path.",
        "The branch now uses direct-authored dual-lobe parameters instead of a larger Unreal-style data package.",
        "Validate highlight response against captures and tune the lobe model."
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
    const char* artifactName;
    const char* artifactSlug;
};

struct App {
    Config config;
    Entity light;
    Material* material = nullptr;
    Material* litMaterial = nullptr;
    MaterialInstance* materialInstance = nullptr;
    MaterialInstance* litMaterialInstance = nullptr;
    Texture* normalMap = nullptr;
    MeshReader::Mesh mesh;
    mat4f transform;
    mat4f baseMeshTransform;

    float3 baseColor = { 1.0f, 1.0f, 1.0f };
    float roughness = 0.5f;
    float metallic = 0.0f;
    float reflectance = 0.5f;
    float thickness = 0.5f;
    float scatteringDistance = 1.2f;   // cm
    float3 subsurfaceColor = { 0.8f, 0.2f, 0.1f };

    float3 meanFreePathColor = { 1.0f, 0.530583f, 0.526042f };
    float meanFreePathDistance = 1.2f;  // cm
    float worldUnitScale = 10.0f;      // cm per scene unit
    float3 falloffColor = { 1.0f, 0.37f, 0.3f };  // UE default skin FalloffColor
    float roughness0 = 0.75f;
    float roughness1 = 1.3f;
    float lobeMix = 0.85f;
    bool temporalNoise = false;
    bool fastSampleNormals = true;
    int sssSampleCount = 64;

    bool screenshotRequested = false;
    bool screenshotCaptureArmed = false;
    bool comparisonCaptureRequested = false;
    bool comparisonCaptureActive = false;
    size_t comparisonCaptureIndex = 0;
    std::vector<CaptureTask> comparisonCaptureTasks;
    std::string comparisonOutputDir;
    std::string gitCommit;

    int viewpointIndex = 0;
    int restoreViewpointIndex = 0;
    float restoreIblIntensity = 30000.0f;

    View* mainView = nullptr;

    // Frame timing
    float frameTimeMs = 0.0f;           // wall-clock frame-to-frame time
    float gpuFrameTimeMs = 0.0f;        // denoised GPU duration from Renderer
    std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();

    // SSS profiler — multi-axis benchmark
    enum class BenchState { IDLE, WARMUP, MEASURE, NEXT, DONE };
    BenchState benchState = BenchState::IDLE;
    int benchFramesLeft = 0;
    int benchSavedSampleCount = 64;
    bool benchSavedFastNormals = true;
    bool benchSavedTemporalNoise = false;

    static constexpr int BENCH_WARMUP_FRAMES = 10;
    static constexpr int BENCH_MEASURE_FRAMES = 30;

    // Each config: {sampleCount, fastNormals, temporalNoise, label}
    struct BenchConfig {
        int sampleCount;  // 0 = SSS blur off, -1 = standard lit material
        bool fastNormals;
        bool temporalNoise;
        const char* label;
    };
    static constexpr int BENCH_CONFIGS = 10;
    static constexpr BenchConfig BENCH_CONFIG_LIST[BENCH_CONFIGS] = {
        { -1, true,  false, "Standard Lit"       },
        {  0, true,  false, "SSS mat, blur OFF"  },
        {  8, true,  false, " 8  fast  det"      },
        { 16, true,  false, "16  fast  det"      },
        { 32, true,  false, "32  fast  det"      },
        { 64, true,  false, "64  fast  det"      },
        {128, true,  false, "128 fast  det"      },
        { 64, false, false, "64  smooth det"     },
        { 64, true,  true,  "64  fast  R2"       },
        { 64, false, true,  "64  smooth R2"      },
    };
    int benchStepIndex = 0;
    float benchAccum = 0.0f;
    int benchCount = 0;
    float benchResults[BENCH_CONFIGS] = {};

    // Running frame time history for sparkline
    static constexpr int FRAME_HISTORY_SIZE = 120;
    float frameHistory[FRAME_HISTORY_SIZE] = {};
    int frameHistoryIdx = 0;

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
    app.scatteringDistance = preset.meanFreePathDistance;  // cm
    app.roughness0 = preset.roughness0;
    app.roughness1 = preset.roughness1;
    app.lobeMix = preset.lobeMix;
}

void writeComparisonMetadata(App const& app, View const* view) {
    std::filesystem::create_directories(app.comparisonOutputDir);

    Camera const& camera = view->getCamera();
    Viewport const viewport = view->getViewport();
    auto const& preset = DEFAULT_PRESET;
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
    out << "  \"roughness0\": " << app.roughness0 << ",\n";
    out << "  \"roughness1\": " << app.roughness1 << ",\n";
    out << "  \"lobeMix\": " << app.lobeMix << "\n";
    out << "}\n";
}

void writeComparisonReport(App const& app) {
    std::ofstream out(app.comparisonOutputDir + "/comparison_report.md", std::ios::trunc);
    out << "# Burley SSS Comparison Report\n\n";
    out << "This capture set is the Filament side of the repeatable Unreal-vs-Filament Burley "
           "comparison workflow.\n\n";
    out << "- Engine commit: `" << app.gitCommit << "`\n";
    out << "- Preset: `" << DEFAULT_PRESET.name << "`\n";
    out << "- Metadata: [`metadata.json`](./metadata.json)\n";
    out << "- Baseline render settings: direct light only, fixed viewpoints, TAA disabled, bloom "
           "disabled, DOF disabled, SSR disabled\n\n";

    out << "## Parameter Mapping\n\n";
    out << "| Unreal Burley term | Filament current mapping | Status |\n";
    out << "| --- | --- | --- |\n";
    out << "| Falloff Color | derives SurfaceAlbedo + DMFP ratios (UE coupled mapping) | Implemented |\n";
    out << "| Mean Free Path Color | material `subsurfaceColor` authored per pixel | Approximate |\n";
    out << "| Mean Free Path Distance | material `scatteringDistance` authored per pixel | Approximate |\n";
    out << "| World Unit Scale | view-level Burley radius calibration | Implemented |\n";
    out << "| Tint | fixed white in current sample flow | Not an active parity lever |\n";
    out << "| Boundary Color Bleed | fixed white in current sample flow; not consumed by engine | Missing in engine |\n";
    out << "| Transmission Tint Color | fixed white in current sample flow | Not an active parity lever |\n";
    out << "| Transmission block | separate thin-region transmission lift driven by thickness, IOR, and current Burley scale | Approximate |\n";
    out << "| Dual Specular | material/light-path dual-lobe specular for Burley shading |\n"
           "      | Implemented |\n\n";

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
    out << "Tighten the Burley scale, `LerpFactor`, and transmission calibration against the "
           "Unreal Burley reference captures before adding more payload complexity.\n";
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
    mi->setParameter("roughness0", app.roughness0);
    mi->setParameter("roughness1", app.roughness1);
    mi->setParameter("lobeMix", app.lobeMix);
}

void applyViewOptions(App& app) {
    if (!app.mainView) {
        return;
    }

    SubsurfaceScatteringOptions sssOptions;
    // sampleCount<=0 is used by benchmark:
    //   0  = SSS material but blur disabled
    //  -1  = standard lit material (blur disabled)
    sssOptions.enabled = app.sssSampleCount > 0;
    sssOptions.sampleCount = uint8_t(std::max(app.sssSampleCount, 8));
    sssOptions.scatteringDistance = 1.0f;
    sssOptions.subsurfaceColor = float3{ 1.0f, 1.0f, 1.0f };
    sssOptions.worldUnitScale = app.worldUnitScale;
    sssOptions.falloffColor = app.falloffColor;
    sssOptions.temporalNoise = app.temporalNoise;
    sssOptions.fastSampleNormals = app.fastSampleNormals;
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

    // Swap material for benchmark standard lit baseline
    if (engine && app.mesh.renderable) {
        auto& rcm = engine->getRenderableManager();
        auto ri = rcm.getInstance(app.mesh.renderable);
        if (ri) {
            if (app.sssSampleCount == -1 && app.litMaterialInstance) {
                rcm.setMaterialInstanceAt(ri, 0, app.litMaterialInstance);
            } else {
                rcm.setMaterialInstanceAt(ri, 0, app.materialInstance);
            }
        }
    }

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
            artifact.name,
            artifact.slug
        });
    }
}

void beginComparisonCapture(App& app) {
    app.restoreViewpointIndex = app.viewpointIndex;
    app.restoreIblIntensity = app.iblIntensity;

    // SSS is always enabled
    app.iblIntensity = 0.0f;
    app.comparisonOutputDir = "captures/sss_burley/" + std::string(DEFAULT_PRESET.slug);
    std::filesystem::create_directories(app.comparisonOutputDir);

    queueComparisonCapture(app);
    app.comparisonCaptureIndex = 0;
    app.comparisonCaptureActive = !app.comparisonCaptureTasks.empty();
    if (app.comparisonCaptureActive) {
        writeComparisonMetadata(app, app.mainView);
        writeComparisonReport(app);
        std::cout << "Starting Burley comparison capture in: " << app.comparisonOutputDir
                  << std::endl;
    }
}

void finishComparisonCapture(App& app) {
    app.comparisonCaptureActive = false;
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
    applyPreset(app, DEFAULT_PRESET);

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
        app.litMaterial = Material::Builder()
            .package(RESOURCES_SANDBOXLIT_DATA, RESOURCES_SANDBOXLIT_SIZE)
            .build(*engine);
        app.litMaterialInstance = app.litMaterial->createInstance();

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
        engine->destroy(app.litMaterialInstance);
        engine->destroy(app.litMaterial);
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
            if (ImGui::Button("Reset to Defaults")) {
                applyPreset(app, DEFAULT_PRESET);
            }

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
            ImGui::SliderFloat("MFP Distance (cm)", &app.scatteringDistance, 0.01f, 5.0f,
                    "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::ColorEdit3("MFP Color", &app.subsurfaceColor.x);
            ImGui::SliderFloat("World Unit Scale (cm/unit)", &app.worldUnitScale, 0.01f, 100.0f,
                    "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::ColorEdit3("Falloff Color", &app.falloffColor.x);
        }

        if (ImGui::CollapsingHeader("Dual Specular")) {
            ImGui::SliderFloat("Dual Spec Roughness 0##DualSpec", &app.roughness0, 0.0f, 1.0f);
            ImGui::SliderFloat("Dual Spec Roughness 1##DualSpec", &app.roughness1, 0.0f, 2.0f);
            ImGui::SliderFloat("Dual Spec Lobe Mix##DualSpec", &app.lobeMix, 0.0f, 1.0f);
        }

        if (ImGui::CollapsingHeader("SSS Blur Pass", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Temporal Noise (R2)", &app.temporalNoise);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                        "R2 quasi-random per-pixel per-frame jitter.\n"
                        "ON: unique sample pattern per pixel, changes each frame (needs TAA).\n"
                        "OFF: deterministic Fibonacci spiral, same pattern every frame.");
            }
            ImGui::Checkbox("Fast Sample Normals", &app.fastSampleNormals);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                        "ON: 1-tap normal per sample (4 reads/sample).\n"
                        "OFF: 5-tap smoothed normal per sample (8 reads/sample).\n"
                        "Center pixel always uses smoothed normal.");
            }
            ImGui::SliderInt("Sample Count", &app.sssSampleCount, 8, 128);
        }

        if (ImGui::CollapsingHeader("Light")) {
            ImGui::ColorEdit3("Light Color", &app.lightColor.x);
            ImGui::SliderFloat("Lux", &app.lightIntensity, 0.0f, 150000.0f, "%.0f",
                    ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Sun Size", &app.sunAngularRadius, 0.1f, 10.0f);
            ImGuiExt::DirectionWidget("Direction", app.lightDirection.v);
            app.lightDirection = normalize(app.lightDirection);
            ImGui::SliderFloat("IBL Intensity", &app.iblIntensity, 0.0f, 50000.0f, "%.0f",
                    ImGuiSliderFlags_Logarithmic);
        }

        if (ImGui::CollapsingHeader("SSS Profiler", ImGuiTreeNodeFlags_DefaultOpen)) {
            // FPS display
            float fps = app.frameTimeMs > 0.0f ? 1000.0f / app.frameTimeMs : 0.0f;
            ImGui::Text("%.1f FPS  %.2f ms  (GPU: %.2f ms)", fps,
                    app.frameTimeMs, app.gpuFrameTimeMs);

            // Viewport info
            uint32_t vpW = 0, vpH = 0;
            if (app.mainView) {
                Viewport vp = app.mainView->getViewport();
                vpW = vp.width;
                vpH = vp.height;
            }

            // Per-pixel cost breakdown
            // Center: color + setupDiffuse + params + albedo + depth + smoothedNormal(5) = 10
            // Per sample: setupDiffuse + depth + albedo + normal(1 or 5) = 4 or 8
            int readsCenter = 10;
            int readsPerSample = app.fastSampleNormals ? 4 : 8;
            int totalReadsPerPixel = readsCenter + app.sssSampleCount * readsPerSample;
            float megaReads = float(vpW) * float(vpH) * float(totalReadsPerPixel) / 1e6f;

            ImGui::Text("Resolution: %u x %u", vpW, vpH);
            ImGui::Text("Samples: %d  |  Reads/sample: %d  |  Reads/px: %d  |  Total: %.0fM",
                    app.sssSampleCount, readsPerSample, totalReadsPerPixel, megaReads);

            // Frame time sparkline
            ImGui::PlotLines("##frametime", app.frameHistory, App::FRAME_HISTORY_SIZE,
                    app.frameHistoryIdx, nullptr, 0.0f, 40.0f, ImVec2(0, 40));

            ImGui::Separator();

            // Multi-axis benchmark
            bool benchRunning = app.benchState != App::BenchState::IDLE
                    && app.benchState != App::BenchState::DONE;
            if (benchRunning) {
                int totalFrames = App::BENCH_CONFIGS
                        * (App::BENCH_WARMUP_FRAMES + App::BENCH_MEASURE_FRAMES);
                int elapsed = app.benchStepIndex
                        * (App::BENCH_WARMUP_FRAMES + App::BENCH_MEASURE_FRAMES)
                        + (App::BENCH_WARMUP_FRAMES + App::BENCH_MEASURE_FRAMES
                                - app.benchFramesLeft);
                ImGui::Text("Benchmarking: %s",
                        App::BENCH_CONFIG_LIST[app.benchStepIndex].label);
                ImGui::ProgressBar(float(elapsed) / float(totalFrames));
            } else {
                if (ImGui::Button("Run Benchmark")) {
                    app.benchSavedSampleCount = app.sssSampleCount;
                    app.benchSavedFastNormals = app.fastSampleNormals;
                    app.benchSavedTemporalNoise = app.temporalNoise;
                    app.benchStepIndex = 0;
                    app.benchState = App::BenchState::WARMUP;
                    app.benchFramesLeft = App::BENCH_WARMUP_FRAMES;
                    auto const& cfg = App::BENCH_CONFIG_LIST[0];
                    app.sssSampleCount = cfg.sampleCount;
                    app.fastSampleNormals = cfg.fastNormals;
                    app.temporalNoise = cfg.temporalNoise;
                    for (int i = 0; i < App::BENCH_CONFIGS; i++) {
                        app.benchResults[i] = 0.0f;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                            "Sweeps sample counts, normal quality, and temporal noise.\n"
                            "%d configs x (%d warmup + %d measure) frames.",
                            App::BENCH_CONFIGS, App::BENCH_WARMUP_FRAMES,
                            App::BENCH_MEASURE_FRAMES);
                }
            }

            // Results table
            bool hasResults = false;
            for (int i = 0; i < App::BENCH_CONFIGS; i++) {
                if (app.benchResults[i] > 0.0f) { hasResults = true; break; }
            }
            if (hasResults) {
                ImGui::Separator();
                ImGui::Columns(4, "benchcols", false);
                ImGui::SetColumnWidth(0, 130);
                ImGui::SetColumnWidth(1, 80);
                ImGui::SetColumnWidth(2, 55);
                ImGui::SetColumnWidth(3, 80);
                ImGui::Text("Config"); ImGui::NextColumn();
                ImGui::Text("ms"); ImGui::NextColumn();
                ImGui::Text("FPS"); ImGui::NextColumn();
                ImGui::Text("vs best"); ImGui::NextColumn();
                ImGui::Separator();

                float bestMs = 1e9f;
                for (int i = 0; i < App::BENCH_CONFIGS; i++) {
                    if (app.benchResults[i] > 0.0f && app.benchResults[i] < bestMs) {
                        bestMs = app.benchResults[i];
                    }
                }

                for (int i = 0; i < App::BENCH_CONFIGS; i++) {
                    if (app.benchResults[i] <= 0.0f) continue;
                    auto const& cfg = App::BENCH_CONFIG_LIST[i];
                    float ms = app.benchResults[i];
                    float delta = ms - bestMs;

                    bool isCurrent = cfg.sampleCount == app.sssSampleCount
                            && cfg.fastNormals == app.fastSampleNormals
                            && cfg.temporalNoise == app.temporalNoise;
                    if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(1.0f, 0.8f, 0.2f, 1.0f));

                    ImGui::Text("%s", cfg.label); ImGui::NextColumn();
                    ImGui::Text("%.2f", ms); ImGui::NextColumn();
                    ImGui::Text("%.0f", 1000.0f / ms); ImGui::NextColumn();
                    if (delta < 0.05f) {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "best");
                    } else {
                        ImGui::Text("+%.2f", delta);
                    }
                    ImGui::NextColumn();
                    if (isCurrent) ImGui::PopStyleColor();
                }
                ImGui::Columns(1);

                // Cost analysis
                // 0=StdLit 1=SSSoff 2=8f 3=16f 4=32f 5=64f 6=128f 7=64s 8=64fR2 9=64sR2
                float litBase = app.benchResults[0];
                float sssBase = app.benchResults[1];
                if (litBase > 0.0f && sssBase > 0.0f) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                            "SSS MRT overhead: %.2f ms (%.1f%%)",
                            sssBase - litBase,
                            100.0f * (sssBase - litBase) / litBase);
                }
                if (sssBase > 0.0f && app.benchResults[5] > 0.0f) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                            "Blur pass @64: %.2f ms",
                            app.benchResults[5] - sssBase);
                }
                if (litBase > 0.0f && app.benchResults[5] > 0.0f) {
                    float total = app.benchResults[5] - litBase;
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                            "Total SSS @64: %.2f ms (%.1f%%)",
                            total,
                            100.0f * total / app.benchResults[5]);
                }
                if (app.benchResults[2] > 0.0f && app.benchResults[6] > 0.0f) {
                    float perSample = (app.benchResults[6] - app.benchResults[2])
                            / float(128 - 8);
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                            "~%.3f ms/sample (fast)", perSample);
                }
                if (app.benchResults[5] > 0.0f && app.benchResults[7] > 0.0f) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                            "Smooth normals @64: +%.2f ms",
                            app.benchResults[7] - app.benchResults[5]);
                }
                if (app.benchResults[5] > 0.0f && app.benchResults[8] > 0.0f) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                            "Temporal noise @64: +%.2f ms",
                            app.benchResults[8] - app.benchResults[5]);
                }
            }
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
        // Wall-clock frame-to-frame time
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float, std::milli>(now - app.lastFrameTime).count();
        app.lastFrameTime = now;
        // Exponential moving average to smooth jitter (alpha ~0.05 → ~20 frame window)
        app.frameTimeMs = app.frameTimeMs > 0.0f
                ? app.frameTimeMs * 0.95f + dt * 0.05f
                : dt;

        // GPU frame time from Filament's Renderer (denoised hardware timer query)
        auto history = renderer->getFrameInfoHistory(1);
        if (!history.empty()) {
            auto const& info = history[0];
            if (info.denoisedGpuFrameDuration != Renderer::FrameInfo::INVALID &&
                    info.denoisedGpuFrameDuration != Renderer::FrameInfo::PENDING) {
                app.gpuFrameTimeMs = float(info.denoisedGpuFrameDuration) / 1e6f;
            }
        }

        // Multi-axis benchmark state machine
        if (app.benchState != App::BenchState::IDLE
                && app.benchState != App::BenchState::DONE) {
            app.benchFramesLeft--;
            switch (app.benchState) {
                case App::BenchState::WARMUP:
                    if (app.benchFramesLeft <= 0) {
                        app.benchState = App::BenchState::MEASURE;
                        app.benchFramesLeft = App::BENCH_MEASURE_FRAMES;
                        app.benchAccum = 0.0f;
                        app.benchCount = 0;
                    }
                    break;
                case App::BenchState::MEASURE:
                    app.benchAccum += dt;
                    app.benchCount++;
                    if (app.benchFramesLeft <= 0) {
                        app.benchResults[app.benchStepIndex] =
                                app.benchAccum / float(app.benchCount);
                        app.benchState = App::BenchState::NEXT;
                    }
                    break;
                case App::BenchState::NEXT:
                    app.benchStepIndex++;
                    if (app.benchStepIndex < App::BENCH_CONFIGS) {
                        auto const& cfg = App::BENCH_CONFIG_LIST[app.benchStepIndex];
                        app.sssSampleCount = cfg.sampleCount;
                        app.fastSampleNormals = cfg.fastNormals;
                        app.temporalNoise = cfg.temporalNoise;
                        app.benchState = App::BenchState::WARMUP;
                        app.benchFramesLeft = App::BENCH_WARMUP_FRAMES;
                    } else {
                        // Restore saved settings
                        app.sssSampleCount = app.benchSavedSampleCount;
                        app.fastSampleNormals = app.benchSavedFastNormals;
                        app.temporalNoise = app.benchSavedTemporalNoise;
                        app.benchState = App::BenchState::DONE;

                        // Save results to file
                        uint32_t vpW = 0, vpH = 0;
                        if (app.mainView) {
                            Viewport vp = app.mainView->getViewport();
                            vpW = vp.width;
                            vpH = vp.height;
                        }
                        std::ofstream out("sss_benchmark.txt", std::ios::trunc);
                        out << "SSS Blur Benchmark Results\n";
                        out << "==========================\n";
                        out << "Resolution: " << vpW << " x " << vpH << "\n";
                        out << "Warmup: " << App::BENCH_WARMUP_FRAMES
                            << " frames, Measure: " << App::BENCH_MEASURE_FRAMES
                            << " frames\n\n";
                        out << std::left << std::setw(22) << "Config"
                            << std::right << std::setw(10) << "ms"
                            << std::setw(10) << "FPS" << "\n";
                        out << std::string(42, '-') << "\n";
                        float baseline = app.benchResults[0];
                        for (int i = 0; i < App::BENCH_CONFIGS; i++) {
                            if (app.benchResults[i] <= 0.0f) continue;
                            auto const& cfg = App::BENCH_CONFIG_LIST[i];
                            float ms = app.benchResults[i];
                            out << std::left << std::setw(22) << cfg.label
                                << std::right << std::fixed << std::setprecision(2)
                                << std::setw(10) << ms
                                << std::setw(10) << (1000.0f / ms) << "\n";
                        }
                        // 0=StdLit 1=SSSoff 2=8f 3=16f 4=32f 5=64f 6=128f
                        // 7=64s 8=64fR2 9=64sR2
                        out << "\nCost Analysis\n";
                        out << std::string(42, '-') << "\n";
                        float litB = app.benchResults[0];
                        float sssB = app.benchResults[1];
                        if (litB > 0.0f && sssB > 0.0f) {
                            out << "SSS MRT overhead:       "
                                << std::fixed << std::setprecision(2)
                                << (sssB - litB) << " ms ("
                                << std::setprecision(1)
                                << (100.0f * (sssB - litB) / litB)
                                << "%)\n";
                        }
                        if (sssB > 0.0f && app.benchResults[5] > 0.0f) {
                            out << "Blur pass @64:          "
                                << std::fixed << std::setprecision(2)
                                << (app.benchResults[5] - sssB) << " ms\n";
                        }
                        if (litB > 0.0f && app.benchResults[5] > 0.0f) {
                            float total = app.benchResults[5] - litB;
                            out << "Total SSS cost @64:     "
                                << std::fixed << std::setprecision(2)
                                << total << " ms ("
                                << std::setprecision(1)
                                << (100.0f * total / app.benchResults[5])
                                << "%)\n";
                        }
                        if (app.benchResults[2] > 0.0f
                                && app.benchResults[6] > 0.0f) {
                            float perSample = (app.benchResults[6]
                                    - app.benchResults[2]) / float(128 - 8);
                            out << "Per-sample cost (fast): "
                                << std::fixed << std::setprecision(3)
                                << perSample << " ms/sample\n";
                        }
                        if (app.benchResults[5] > 0.0f
                                && app.benchResults[7] > 0.0f) {
                            out << "Smooth normals @64:    +"
                                << std::fixed << std::setprecision(2)
                                << (app.benchResults[7] - app.benchResults[5])
                                << " ms\n";
                        }
                        if (app.benchResults[5] > 0.0f
                                && app.benchResults[8] > 0.0f) {
                            out << "Temporal noise @64:    +"
                                << std::fixed << std::setprecision(2)
                                << (app.benchResults[8] - app.benchResults[5])
                                << " ms\n";
                        }
                        out.close();
                        std::cout << "Benchmark saved to sss_benchmark.txt"
                                  << std::endl;
                    }
                    break;
                default:
                    break;
            }
        }

        // Record frame time into sparkline history
        app.frameHistory[app.frameHistoryIdx] = dt;
        app.frameHistoryIdx = (app.frameHistoryIdx + 1) % App::FRAME_HISTORY_SIZE;

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
            if (app.comparisonCaptureIndex >= app.comparisonCaptureTasks.size()) {
                finishComparisonCapture(app);
            }
        }
    };

    FilamentApp::get().run(app.config, setup, cleanup, imgui,
            FilamentApp::PreRenderCallback(), postRender);

    return 0;
}
