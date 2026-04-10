/*
 * Copyright (C) 2026
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
#include "common/configuration.h"

#include <filament/Camera.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/View.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>
#include <filamentapp/IBL.h>

#include <filamat/MaterialBuilder.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>

#include <imgui.h>
#include <stb_image.h>

#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>
#include <utils/Path.h>
#include <utils/getopt.h>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec3.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace filament;
using namespace filament::math;
using namespace filament::gltfio;
using namespace filamat;
using namespace utils;

namespace {

static constexpr const char* DEFAULT_IBL = "assets/ibl/lightroom_14b";
static constexpr const char* CHARACTER_GLTF = "assets/models/character/Model.gltf";
static constexpr const char* HEAD_ENTITY_NAME = "Head_Geo";
static constexpr const char* USER_TYPO_ENTITY_NAME = "header_geo";

struct App {
    Config config;
    NameComponentManager* names = nullptr;
    MaterialProvider* materials = nullptr;
    AssetLoader* assetLoader = nullptr;
    ResourceLoader* resourceLoader = nullptr;
    FilamentAsset* asset = nullptr;

    Material* skinMaterial = nullptr;
    MaterialInstance* skinMaterialInstance = nullptr;

    Texture* baseColorMap = nullptr;
    Texture* normalMap = nullptr;
    Texture* roughnessMap = nullptr;
    Texture* aoMap = nullptr;
    Texture* specularMap = nullptr;
    Texture* thicknessMap = nullptr;

    Entity keyLight = {};

    View::SkinSSSOptions skinSSS = {
            .enabled = true,
            .quality = View::SkinSSSOptions::Quality::FULL,
            .strength = 1.0f,
            .scale = 1.0f
    };
    float thicknessScale = 1.25f;
    float scatterDistance = 1.10f;
    float scatterStrength = 0.85f;
    float3 scatterTint = float3{ 1.0f, 0.53f, 0.45f };
} g_app;

std::ifstream::pos_type getFileSize(const char* filename) {
    std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
    return in.tellg();
}

std::vector<uint8_t> readFile(const Path& path) {
    long const contentSize = static_cast<long>(getFileSize(path.c_str()));
    if (contentSize <= 0) {
        std::cerr << "Unable to open " << path << std::endl;
        return {};
    }
    std::ifstream in(path.c_str(), std::ifstream::binary | std::ifstream::in);
    std::vector<uint8_t> buffer(static_cast<size_t>(contentSize));
    if (!in.read(reinterpret_cast<char*>(buffer.data()), contentSize)) {
        std::cerr << "Unable to read " << path << std::endl;
        return {};
    }
    return buffer;
}

Texture* loadTexture(Engine* engine, const Path& path, bool srgb) {
    int w, h, n;
    unsigned char* data = stbi_load(path.getAbsolutePath().c_str(), &w, &h, &n, 4);
    if (!data) {
        std::cerr << "Unable to load texture " << path << std::endl;
        return nullptr;
    }
    Texture* texture = Texture::Builder()
            .width(uint32_t(w))
            .height(uint32_t(h))
            .levels(0xff)
            .format(srgb ? Texture::InternalFormat::SRGB8_A8 : Texture::InternalFormat::RGBA8)
            .usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE)
            .build(*engine);
    Texture::PixelBufferDescriptor buffer(data, size_t(w * h * 4),
            Texture::Format::RGBA, Texture::Type::UBYTE,
            (Texture::PixelBufferDescriptor::Callback)&stbi_image_free);
    texture->setImage(*engine, 0, std::move(buffer));
    texture->generateMipmaps(*engine);
    return texture;
}

void updateSkinParameters(View* view) {
    if (!g_app.skinMaterialInstance) {
        return;
    }
    g_app.skinMaterialInstance->setParameter("thicknessScale", g_app.thicknessScale);
    g_app.skinMaterialInstance->setParameter("skinScatterDistance", g_app.scatterDistance);
    g_app.skinMaterialInstance->setParameter("skinScatterStrength", g_app.scatterStrength);
    g_app.skinMaterialInstance->setParameter("skinScatterTint", RgbType::LINEAR, g_app.scatterTint);
    view->setSkinSSSOptions(g_app.skinSSS);
}

Material* createSkinMaterial(Engine* engine) {
    MaterialBuilder::init();
    MaterialBuilder builder;
    builder.name("SampleSkin")
            .targetApi(MaterialBuilder::TargetApi::ALL)
#ifndef NDEBUG
            .optimization(MaterialBuilderBase::Optimization::NONE)
#endif
            .require(VertexAttribute::UV0)
            .parameter("baseColorMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("normalMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("roughnessMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("aoMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("specularMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("thicknessMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("thicknessScale", MaterialBuilder::UniformType::FLOAT)
            .parameter("skinScatterDistance", MaterialBuilder::UniformType::FLOAT)
            .parameter("skinScatterStrength", MaterialBuilder::UniformType::FLOAT)
            .parameter("skinScatterTint", MaterialBuilder::UniformType::FLOAT3)
            .material(R"SHADER(
                void material(inout MaterialInputs material) {
                    vec2 uv = getUV0();
                    material.normal = texture(materialParams_normalMap, uv).xyz * 2.0 - 1.0;
                    prepareMaterial(material);

                    vec4 baseColor = texture(materialParams_baseColorMap, uv);
                    float roughness = texture(materialParams_roughnessMap, uv).r;
                    float ao = texture(materialParams_aoMap, uv).r;
                    float specular = texture(materialParams_specularMap, uv).r;
                    float thickness = texture(materialParams_thicknessMap, uv).r *
                            materialParams_thicknessScale;

                    material.baseColor = vec4(baseColor.rgb, 1.0);
                    material.ambientOcclusion = ao;
                    material.roughness = roughness;
                    material.metallic = 0.0;
                    material.reflectance = mix(0.35, 0.75, specular);
                    material.thickness = thickness;
                    material.skinMask = 1.0;
                    material.skinScatterDistance = materialParams_skinScatterDistance;
                    material.skinScatterStrength = materialParams_skinScatterStrength;
                    material.skinScatterTint = materialParams_skinScatterTint;
                }
            )SHADER")
            .shading(Shading::SKIN);

    Package package = builder.build(engine->getJobSystem());
    if (!package.isValid()) {
        std::cerr << "Unable to compile skin material package." << std::endl;
        return nullptr;
    }
    return Material::Builder().package(package.getData(), package.getSize()).build(*engine);
}

void applySkinMaterialToHead(Engine* engine) {
    if (!g_app.asset || !g_app.skinMaterialInstance) {
        return;
    }

    Entity head = g_app.asset->getFirstEntityByName(HEAD_ENTITY_NAME);
    if (!head) {
        head = g_app.asset->getFirstEntityByName(USER_TYPO_ENTITY_NAME);
    }
    if (!head) {
        std::cerr << "Unable to find Head_Geo renderable in character asset." << std::endl;
        return;
    }

    auto& rcm = engine->getRenderableManager();
    auto instance = rcm.getInstance(head);
    if (!instance) {
        std::cerr << "Head_Geo exists but is not renderable." << std::endl;
        return;
    }
    size_t const primitiveCount = rcm.getPrimitiveCount(instance);
    for (size_t primitive = 0; primitive < primitiveCount; ++primitive) {
        rcm.setMaterialInstanceAt(instance, uint8_t(primitive), g_app.skinMaterialInstance);
    }
}

void frameAsset(Engine* engine, View* view) {
    Aabb aabb = g_app.asset->getBoundingBox();
    float3 extent = aabb.extent();
    float const maxExtent = std::max({extent.x, extent.y, extent.z});
    float const scale = maxExtent > 0.0f ? 2.0f / maxExtent : 1.0f;
    float3 const center = aabb.center();

    auto& tcm = engine->getTransformManager();
    auto root = tcm.getInstance(g_app.asset->getRoot());
    tcm.setTransform(root, mat4f{ mat3f(scale), -center * scale });

    Camera& camera = view->getCamera();
    camera.setExposure(16.0f, 1.0f / 125.0f, 100.0f);
    camera.lookAt(float3{ 0.0f, 0.1f, 3.1f }, float3{ 0.0f, 0.1f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f });
}

void printUsage(char* name) {
    std::string execName(Path(name).getName());
    std::string usage(
            "SAMPLE_SKIN loads the character glTF and applies a runtime skin material to Head_Geo\n"
            "Usage:\n"
            "    SAMPLE_SKIN [options]\n"
            "Options:\n"
            "   --help, -h\n"
            "       Prints this message\n\n"
            "API_USAGE"
            "   --ibl=<path to cmgen IBL>, -i <path>\n"
            "       Override the built-in IBL\n\n");
    const std::string from("SAMPLE_SKIN");
    for (size_t pos = usage.find(from); pos != std::string::npos; pos = usage.find(from, pos)) {
        usage.replace(pos, from.length(), execName);
    }
    const std::string apiUsage("API_USAGE");
    for (size_t pos = usage.find(apiUsage); pos != std::string::npos; pos = usage.find(apiUsage, pos)) {
        usage.replace(pos, apiUsage.length(), samples::getBackendAPIArgumentsUsage());
    }
    std::cout << usage;
}

int handleCommandLineArguments(int argc, char* argv[], Config* config) {
    static constexpr const char* OPTSTR = "ha:i:";
    static const utils::getopt::option OPTIONS[] = {
            { "help", utils::getopt::no_argument, nullptr, 'h' },
            { "api",  utils::getopt::required_argument, nullptr, 'a' },
            { "ibl",  utils::getopt::required_argument, nullptr, 'i' },
            { nullptr, 0, nullptr, 0 }
    };
    int opt;
    int optionIndex = 0;
    while ((opt = utils::getopt::getopt_long(argc, argv, OPTSTR, OPTIONS, &optionIndex)) >= 0) {
        std::string arg(utils::getopt::optarg ? utils::getopt::optarg : "");
        switch (opt) {
            case 'a':
                config->backend = samples::parseArgumentsForBackend(arg);
                break;
            case 'i':
                config->iblDirectory = arg;
                break;
            case 'h':
            default:
                printUsage(argv[0]);
                exit(0);
        }
    }
    return utils::getopt::optind;
}

} // namespace

int main(int argc, char** argv) {
    g_app.config.title = "Skin Sample";
    g_app.config.iblDirectory = FilamentApp::getRootAssetsPath() + DEFAULT_IBL;
    handleCommandLineArguments(argc, argv, &g_app.config);

    auto setup = [](Engine* engine, View* view, Scene* scene) {
        Path const root = FilamentApp::getRootAssetsPath();
        Path const gltfPath = root + CHARACTER_GLTF;

        g_app.names = new NameComponentManager(EntityManager::get());
        g_app.materials = createJitShaderProvider(engine, false,
                samples::getJitMaterialVariantFilter(g_app.config.backend));
        g_app.assetLoader = AssetLoader::create({ engine, g_app.materials, g_app.names });

        std::vector<uint8_t> gltf = readFile(gltfPath);
        if (gltf.empty()) {
            std::cerr << "Unable to load character glTF " << gltfPath << std::endl;
            exit(1);
        }

        g_app.asset = g_app.assetLoader->createAsset(gltf.data(), uint32_t(gltf.size()));
        if (!g_app.asset) {
            std::cerr << "Unable to parse character glTF " << gltfPath << std::endl;
            exit(1);
        }

        ResourceConfiguration configuration = {};
        std::string const absoluteGltfPath = gltfPath.getAbsolutePath();
        configuration.engine = engine;
        configuration.gltfPath = absoluteGltfPath.c_str();
        configuration.normalizeSkinningWeights = true;
        g_app.resourceLoader = new ResourceLoader(configuration);
        if (!g_app.resourceLoader->loadResources(g_app.asset)) {
            std::cerr << "Unable to load character resources for " << gltfPath << std::endl;
            exit(1);
        }
        g_app.asset->releaseSourceData();
        scene->addEntities(g_app.asset->getEntities(), g_app.asset->getEntityCount());

        if (auto* ibl = FilamentApp::get().getIBL()) {
            scene->setIndirectLight(ibl->getIndirectLight());
            scene->setSkybox(ibl->getSkybox());
        }

        g_app.skinMaterial = createSkinMaterial(engine);
        if (!g_app.skinMaterial) {
            exit(1);
        }
        g_app.skinMaterialInstance = g_app.skinMaterial->createInstance();

        TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                TextureSampler::MagFilter::LINEAR, TextureSampler::WrapMode::REPEAT);
        sampler.setAnisotropy(8.0f);

        g_app.baseColorMap = loadTexture(engine, root + "assets/models/character/head_color.png", true);
        g_app.normalMap = loadTexture(engine, root + "assets/models/character/head_normal.png", false);
        g_app.roughnessMap = loadTexture(engine, root + "assets/models/character/head_roughness.png", false);
        g_app.aoMap = loadTexture(engine, root + "assets/models/character/head_ao.png", false);
        g_app.specularMap = loadTexture(engine, root + "assets/models/character/head_specular.png", false);
        g_app.thicknessMap = loadTexture(engine, root + "assets/models/character/head_thickness.png", false);

        if (!g_app.baseColorMap || !g_app.normalMap || !g_app.roughnessMap ||
                !g_app.aoMap || !g_app.specularMap || !g_app.thicknessMap) {
            std::cerr << "Unable to load one or more character textures." << std::endl;
            exit(1);
        }

        g_app.skinMaterialInstance->setParameter("baseColorMap", g_app.baseColorMap, sampler);
        g_app.skinMaterialInstance->setParameter("normalMap", g_app.normalMap, sampler);
        g_app.skinMaterialInstance->setParameter("roughnessMap", g_app.roughnessMap, sampler);
        g_app.skinMaterialInstance->setParameter("aoMap", g_app.aoMap, sampler);
        g_app.skinMaterialInstance->setParameter("specularMap", g_app.specularMap, sampler);
        g_app.skinMaterialInstance->setParameter("thicknessMap", g_app.thicknessMap, sampler);
        updateSkinParameters(view);
        applySkinMaterialToHead(engine);
        frameAsset(engine, view);

        g_app.keyLight = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
                .color(Color::toLinear<ACCURATE>({0.98f, 0.89f, 0.85f}))
                .intensity(120000.0f)
                .direction({0.45f, -1.0f, -0.35f})
                .castShadows(true)
                .build(*engine, g_app.keyLight);
        scene->addEntity(g_app.keyLight);
    };

    auto cleanup = [](Engine* engine, View*, Scene*) {
        engine->destroy(g_app.skinMaterialInstance);
        engine->destroy(g_app.skinMaterial);
        engine->destroy(g_app.baseColorMap);
        engine->destroy(g_app.normalMap);
        engine->destroy(g_app.roughnessMap);
        engine->destroy(g_app.aoMap);
        engine->destroy(g_app.specularMap);
        engine->destroy(g_app.thicknessMap);
        if (g_app.keyLight) {
            engine->destroy(g_app.keyLight);
            EntityManager::get().destroy(g_app.keyLight);
        }

        if (g_app.assetLoader && g_app.asset) {
            g_app.assetLoader->destroyAsset(g_app.asset);
        }

        delete g_app.resourceLoader;
        delete g_app.materials;
        delete g_app.names;
        AssetLoader::destroy(&g_app.assetLoader);
    };

    auto gui = [](Engine*, View* view) {
        ImGui::Begin("Skin");

        int quality = int(g_app.skinSSS.quality);
        if (ImGui::Combo("SSS Quality", &quality, "Off\0Cheap\0Full\0")) {
            g_app.skinSSS.quality = View::SkinSSSOptions::Quality(quality);
            g_app.skinSSS.enabled = g_app.skinSSS.quality != View::SkinSSSOptions::Quality::OFF;
            updateSkinParameters(view);
        }

        if (ImGui::SliderFloat("SSS Strength", &g_app.skinSSS.strength, 0.0f, 1.0f)) {
            updateSkinParameters(view);
        }
        if (ImGui::SliderFloat("SSS Scale", &g_app.skinSSS.scale, 0.1f, 4.0f)) {
            updateSkinParameters(view);
        }
        if (ImGui::SliderFloat("Thickness Scale", &g_app.thicknessScale, 0.0f, 4.0f)) {
            updateSkinParameters(view);
        }
        if (ImGui::SliderFloat("Scatter Distance", &g_app.scatterDistance, 0.0f, 3.0f)) {
            updateSkinParameters(view);
        }
        if (ImGui::SliderFloat("Scatter Strength", &g_app.scatterStrength, 0.0f, 1.0f)) {
            updateSkinParameters(view);
        }
        if (ImGui::ColorEdit3("Scatter Tint", &g_app.scatterTint.r)) {
            updateSkinParameters(view);
        }

        ImGui::Text("Applied to: %s", HEAD_ENTITY_NAME);
        ImGui::Text("Model: %s", CHARACTER_GLTF);
        ImGui::End();
    };

    FilamentApp::get().run(g_app.config, setup, cleanup, gui);
    return 0;
}
