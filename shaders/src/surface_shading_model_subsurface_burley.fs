/**
 * Evaluates lit materials with the Burley normalized diffusion subsurface scattering model.
 *
 * This model uses the Burley (Disney) normalized diffusion profile for subsurface scattering.
 * The radially symmetric reflectance profile R(r) is:
 *     R(r) = A * s / (8 * pi * r) * (exp(-s * r) + exp(-s * r / 3))
 * where s = 1.0 / scatteringDistance, A = subsurfaceColor.
 *
 * The screen-space blur is handled in the post-processing pipeline (PostProcessManager).
 * Keep the direct path close to regular diffuse + specular lighting and let the screen-space
 * Burley pass form the visible scattering energy. The setup buffer should carry real diffuse
 * lighting that can plausibly scatter, while the post-process pass handles broadening,
 * tinting, and thin-region transmission.
 */
vec3 surfaceShading(const PixelParams pixel, const Light light, float occlusion) {
    g_sssMask = max(g_sssMask, max(pixel.subsurfaceColor.r,
            max(pixel.subsurfaceColor.g, pixel.subsurfaceColor.b)));

    vec3 h = normalize(shading_view + light.l);
    float NoL = light.NoL;
    float NoH = saturate(dot(shading_normal, h));
    float LoH = saturate(dot(light.l, h));

    vec3 Fr = vec3(0.0);
    if (NoL > 0.0) {
        Fr = burleyDualSpecularLobe(pixel, h, shading_NoV, NoL, NoH, LoH) *
                pixel.energyCompensation;
    }

    // diffuse BRDF
    vec3 Fd = pixel.diffuseColor * diffuse(pixel.roughness, shading_NoV, NoL, LoH);
    vec3 color = (Fd + Fr) * (NoL * occlusion);

    vec3 result = (color * light.colorIntensity.rgb) * (light.colorIntensity.w * light.attenuation);

    // Seed the SSS setup with the actual direct diffuse energy that should later broaden under
    // the surface. Keep specular out of this payload so the post-process pass can preserve it.
    vec3 scatterableDiffuse = Fd * (NoL * occlusion);
    scatterableDiffuse = (scatterableDiffuse * light.colorIntensity.rgb) *
            (light.colorIntensity.w * light.attenuation);
    g_sssDiffuse += scatterableDiffuse;

    return result;
}
