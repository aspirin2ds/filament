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
 * Burley pass form the visible scattering band. A baked-in wrap / forward-scatter approximation
 * tends to pre-lift the shadow side and makes the final band much harder to read.
 */
vec3 surfaceShading(const PixelParams pixel, const Light light, float occlusion) {
    g_sssMask = max(g_sssMask, max(pixel.subsurfaceColor.r,
            max(pixel.subsurfaceColor.g, pixel.subsurfaceColor.b)));

    vec3 h = normalize(shading_view + light.l);
    vec3 macroNormal = getWorldGeometricNormalVector();

    float rawNoL = dot(shading_normal, light.l);
    float NoL = light.NoL;
    float NoH = saturate(dot(shading_normal, h));
    float LoH = saturate(dot(light.l, h));

    vec3 Fr = vec3(0.0);
    if (NoL > 0.0) {
        // specular BRDF
        float D = distribution(pixel.roughness, NoH, h);
        float V = visibility(pixel.roughness, shading_NoV, NoL);
        vec3  F = fresnel(pixel.f0, LoH);
        Fr = (D * V) * F * pixel.energyCompensation;
    }

    // diffuse BRDF
    vec3 Fd = pixel.diffuseColor * diffuse(pixel.roughness, shading_NoV, NoL, LoH);

    // Seed the screen-space pass with a narrow, light-driven band centered slightly on the
    // shadow side of the terminator. This keeps the effect anchored to the light / shadow
    // boundary rather than flooding the entire lit side with blur energy.
    float macroRawNoL = dot(macroNormal, light.l);
    float scatterStrength = saturate(pixel.scatteringDistance * 5.0);
    float bandHalfWidth = mix(0.10, 0.22, scatterStrength);
    float bandCenter = -bandHalfWidth * 0.12;
    float bandShape = 1.0 - smoothstep(0.0, bandHalfWidth, abs(macroRawNoL - bandCenter));
    float litShoulder = 1.0 - smoothstep(0.0, bandHalfWidth * 2.25, macroRawNoL);
    float thicknessScale = mix(1.0, 0.55, pixel.thickness);
    vec3 terminatorLift = pixel.diffuseColor * pixel.subsurfaceColor *
            (bandShape * litShoulder * thicknessScale * 0.38);

    // Apply NoL and occlusion to front-facing lighting while letting the SSS seed softly lift
    // the shadow-side boundary.
    vec3 color = ((Fd + Fr) * NoL + terminatorLift) * occlusion;

    vec3 result = (color * light.colorIntensity.rgb) * (light.colorIntensity.w * light.attenuation);

    // Keep the blur source concentrated near the terminator. A tiny lit-side shoulder helps the
    // blur borrow energy from the lit side without turning the whole front-facing hemisphere into
    // a scattering source.
    vec3 edgeDiffuse = Fd * NoL * litShoulder * 0.32;
    vec3 scatterableDiffuse = (terminatorLift + edgeDiffuse) * occlusion;
    scatterableDiffuse = (scatterableDiffuse * light.colorIntensity.rgb) *
            (light.colorIntensity.w * light.attenuation);
    g_sssDiffuse += scatterableDiffuse;

    return result;
}
