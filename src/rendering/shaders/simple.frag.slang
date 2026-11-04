struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float3 pbrParams : TEXCOORD0;
};

float4 main(FragmentInput input) : SV_Target {
    float alpha = input.pbrParams.z;
    if (length(input.normal) < 0.01) {
        // Unlit CAD lines (grid, XYZ axes, wireframes)
        return float4(input.color, alpha);
    }

    float3 norm = normalize(input.normal);
    float3 baseColor = input.color;
    float metallic = clamp(input.pbrParams.x, 0.0, 1.0);
    float roughness = clamp(input.pbrParams.y, 0.04, 1.0);

    // Studio CAD Ambient Hemisphere Lighting
    float3 skyColor = float3(0.38, 0.40, 0.44);
    float3 groundColor = float3(0.18, 0.16, 0.15);
    float hemi = norm.y * 0.5 + 0.5;
    float3 ambient = lerp(groundColor, skyColor, hemi);

    // Studio Key Light (warm upper-right)
    float3 keyLightDir = normalize(float3(0.6, 0.9, 0.7));
    float diff1 = max(dot(norm, keyLightDir), 0.0) * 0.60;

    // Studio Fill Light (cool lower-left)
    float3 fillLightDir = normalize(float3(-0.5, -0.3, -0.6));
    float diff2 = max(dot(norm, fillLightDir), 0.0) * 0.20;

    float3 diffuse = baseColor * (ambient + diff1 + diff2) * (1.0 - metallic * 0.65);

    // Studio View Direction (front-top CAD viewpoint)
    float3 viewDir = normalize(float3(0.0, 0.4, 0.9));

    // Specular Highlight (Blinn-Phong / GGX approximation for mobile CAD)
    float3 halfKey = normalize(keyLightDir + viewDir);
    float NdotH = max(dot(norm, halfKey), 0.0);
    float specPower = pow(2.0, (1.0 - roughness) * 8.0);
    float specFactor = pow(NdotH, specPower);

    // Dielectric / Conductor Fresnel lerp
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 specular = F0 * specFactor * (1.0 - roughness * 0.5) * 0.85;

    // Subtle Studio Reflection (horizon / sky reflection on smooth metals)
    float reflectionGradient = max(0.0, norm.y);
    float3 studioReflect = lerp(float3(0.12, 0.13, 0.15), float3(0.40, 0.45, 0.55), reflectionGradient);
    float3 reflection = studioReflect * F0 * metallic * (1.0 - roughness);

    float3 finalColor = diffuse + specular + reflection;
    return float4(finalColor, alpha);
}
