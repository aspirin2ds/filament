vec3 customSurfaceShading(const MaterialInputs materialInputs,
        const PixelParams pixel, const Light light, float visibility) {

    LightData lightData;
    lightData.colorIntensity = light.colorIntensity;
    lightData.l = light.l;
    lightData.NdotL = light.NoL;
    lightData.worldPosition = light.worldPosition;
    lightData.attenuation = light.attenuation;
    lightData.visibility = visibility;

    ShadingData shadingData;
    shadingData.diffuseColor = pixel.diffuseColor;
    shadingData.perceptualRoughness = pixel.perceptualRoughness;
    shadingData.f0 = pixel.f0;
    shadingData.roughness = pixel.roughness;

    vec3 color = surfaceShading(materialInputs, shadingData, lightData);

#if defined(SHADING_MODEL_SUBSURFACE_BURLEY)
    g_sssMask = max(g_sssMask, max(pixel.subsurfaceColor.r,
            max(pixel.subsurfaceColor.g, pixel.subsurfaceColor.b)));

    vec3 h = normalize(shading_view + light.l);
    float NoL = light.NoL;
    float frontLightMask = step(0.0, dot(shading_geometricNormal, light.l));
    float LoH = saturate(dot(light.l, h));
    vec3 Fd = pixel.diffuseColor * diffuse(pixel.roughness, shading_NoV, NoL, LoH);
    vec3 scatterableDiffuse = (Fd * (NoL * visibility * frontLightMask)) *
            (light.colorIntensity.rgb * (light.colorIntensity.w * light.attenuation));
    g_sssDiffuse += scatterableDiffuse;
#endif

    return color;
}
