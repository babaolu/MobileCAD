struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float4 pbrParams : TEXCOORD0; // x: metallic, y: roughness, z: useMaterialColor, w: alpha
};

float4 main(FragmentInput input) : SV_Target {
    float alpha = input.pbrParams.w;
    if (length(input.normal) < 0.01) {
        // Unlit CAD lines (grid, XYZ axes, wireframes)
        return float4(input.color, alpha);
    }

    float3 norm = normalize(input.normal);
    float3 baseColor = input.color;
    float metallic = clamp(input.pbrParams.x, 0.0, 1.0);
    float roughness = clamp(input.pbrParams.y, 0.04, 1.0);
    float useMaterialColor = input.pbrParams.z;

    // Camera-relative View Direction in Camera View Space
    // The camera sits at (0, 0, 0) looking down -Z, so looking towards the eye is +Z
    float3 viewDir = normalize(float3(0.0, 0.0, 1.0));

    // Camera-rigged studio lighting setup:
    // Key Light 1 (upper-right front)
    float3 keyLightDir = normalize(float3(0.45, 0.65, 0.60));
    float diff1 = max(dot(norm, keyLightDir), 0.0) * 0.65;

    // Fill Light 2 (lower-left front)
    float3 fillLightDir = normalize(float3(-0.55, -0.35, 0.75));
    float diff2 = max(dot(norm, fillLightDir), 0.0) * 0.25;

    // Top Rim Light (subtle top-back highlight)
    float3 rimLightDir = normalize(float3(0.0, 0.85, -0.50));
    float diffRim = max(dot(norm, rimLightDir), 0.0) * 0.15;

    // Studio Ambient Hemisphere (sky from top, warm ground from below)
    float3 skyColor = float3(0.35, 0.38, 0.42);
    float3 groundColor = float3(0.16, 0.15, 0.14);
    float hemi = norm.y * 0.5 + 0.5;
    float3 ambient = lerp(groundColor, skyColor, hemi);

    // Subtle per-face normal directional tint for crisp CAD edge contrast
    // Ensures adjacent orthogonal faces (+X, +Y, +Z) of cubes have clear tonal separation
    float faceContrast = 1.0;
    if (useMaterialColor > 0.5) {
        faceContrast = 0.88 + 0.16 * norm.y + 0.08 * norm.x - 0.06 * norm.z;
    }

    float3 diffuse = baseColor * faceContrast * (ambient + diff1 + diff2 + diffRim) * (1.0 - metallic * 0.65);

    // Camera-Relative Specular Highlights (Blinn-Phong / GGX approximation)
    float specPower = pow(2.0, (1.0 - roughness) * 8.5);

    float3 halfKey = normalize(keyLightDir + viewDir);
    float NdotH1 = max(dot(norm, halfKey), 0.0);
    float specFactor1 = pow(NdotH1, specPower);

    float3 halfFill = normalize(fillLightDir + viewDir);
    float NdotH2 = max(dot(norm, halfFill), 0.0);
    float specFactor2 = pow(NdotH2, specPower * 0.65);

    // Dielectric (0.04) vs Metal (baseColor) Fresnel F0
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 specular = (F0 * specFactor1 * 0.85 + F0 * specFactor2 * 0.30) * (1.0 - roughness * 0.5);

    // Dynamic Camera-Relative Studio Horizon Reflection for polished metals
    float3 reflectDir = reflect(-viewDir, norm);
    float reflectionGrad = max(0.0, reflectDir.y);
    float3 studioReflect = lerp(float3(0.12, 0.13, 0.15), float3(0.45, 0.50, 0.60), reflectionGrad);
    float3 reflection = studioReflect * F0 * metallic * (1.0 - roughness);

    float3 finalColor = diffuse + specular + reflection;
    return float4(finalColor, alpha);
}
