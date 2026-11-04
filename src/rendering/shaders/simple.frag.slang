struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float4 pbrParams : TEXCOORD0; // x: metallic, y: roughness, z: useMaterialColor, w: alpha
    [[vk::location(3)]] float3 viewPos : TEXCOORD1;
};

// GGX Normal Distribution Function
float D_GGX(float NdotH, float a2) {
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (3.1415926535 * denom * denom + 1e-5);
}

// Schlick-GGX Geometry Function
float G_Schlick(float NdotV, float NdotL, float k) {
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / (NdotL * (1.0 - k) + k);
    return gV * gL;
}

// Schlick Fresnel Function
float3 F_Schlick(float VdotH, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

float4 main(FragmentInput input) : SV_Target {
    float alpha = input.pbrParams.w;
    if (length(input.normal) < 0.01) {
        // Unlit CAD lines (grid, XYZ axes, wireframes)
        return float4(input.color, alpha);
    }

    float3 N = normalize(input.normal);

    // True per-pixel view vector pointing towards camera (origin) in view space
    float3 V = normalize(-input.viewPos);
    if (dot(V, V) < 1e-4) {
        V = float3(0.0, 0.0, 1.0);
    }

    // Two-sided normal orientation
    if (dot(N, V) < 0.0) {
        N = -N;
    }

    float3 baseColor = input.color;
    float metallic = clamp(input.pbrParams.x, 0.0, 1.0);
    float roughness = clamp(input.pbrParams.y, 0.04, 1.0);
    float useMaterialColor = input.pbrParams.z;

    float alphaRoughness = roughness * roughness;
    float a2 = max(alphaRoughness * alphaRoughness, 1e-4);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;

    float NdotV = max(dot(N, V), 0.001);

    // Dielectric (0.04) vs Conductor (baseColor) Fresnel F0
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    // Studio 3-Point Light Rig:
    // Key Light 1 (upper-right front, warm white)
    float3 L1 = normalize(float3(0.45, 0.65, 0.60));
    float3 col1 = float3(1.0, 0.97, 0.92) * 1.05;

    // Fill Light 2 (lower-left front, cool white)
    float3 L2 = normalize(float3(-0.55, -0.30, 0.70));
    float3 col2 = float3(0.88, 0.94, 1.0) * 0.40;

    // Rim Light 3 (back-top, crisp white)
    float3 L3 = normalize(float3(0.0, 0.85, -0.50));
    float3 col3 = float3(0.95, 0.98, 1.0) * 0.25;

    float3 directLighting = float3(0.0, 0.0, 0.0);

    // Evaluate direct analytical lights
    float3 lightDirs[3] = {L1, L2, L3};
    float3 lightColors[3] = {col1, col2, col3};

    for (int i = 0; i < 3; ++i) {
        float3 L = lightDirs[i];
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float D = D_GGX(NdotH, a2);
            float G = G_Schlick(NdotV, NdotL, k);
            float3 F = F_Schlick(VdotH, F0);

            float3 specBRDF = (D * G * F) / (4.0 * NdotV * NdotL + 0.001);
            float3 kD = (float3(1.0) - F) * (1.0 - metallic);
            float3 diffBRDF = kD * baseColor / 3.1415926535;

            directLighting += (diffBRDF + specBRDF) * lightColors[i] * NdotL;
        }
    }

    // High-Contrast Procedural Studio HDR Environment Map along reflection vector R
    float3 R = reflect(-V, N);

    // Studio softbox light banks overhead
    float softbox1 = smoothstep(0.35, 0.75, R.y) * smoothstep(0.65, 0.05, abs(R.x));
    float softbox2 = smoothstep(0.20, 0.65, -R.x) * smoothstep(0.45, 0.0, abs(R.y));
    float softbox3 = smoothstep(0.25, 0.70, R.x) * smoothstep(0.50, 0.0, abs(R.z));

    // Studio horizon and ground gradient
    float horizonGrad = smoothstep(-0.25, 0.35, R.y);
    float3 studioSky = lerp(float3(0.09, 0.10, 0.12), float3(0.42, 0.46, 0.54), horizonGrad);
    float3 studioGround = float3(0.14, 0.13, 0.13);
    float3 envBase = lerp(studioGround, studioSky, clamp(R.y * 0.5 + 0.5, 0.0, 1.0));

    float3 studioEnv = envBase +
                       float3(1.40, 1.35, 1.30) * softbox1 * 1.6 +
                       float3(0.85, 0.95, 1.10) * softbox2 * 0.7 +
                       float3(0.90, 0.92, 0.98) * softbox3 * 0.5;

    // Modulate environment reflection by roughness and Fresnel
    float3 F_env = F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
    float3 envSpecular = studioEnv * F_env * (1.0 - roughness * 0.65);

    // Studio Ambient Hemisphere Diffuse
    float3 ambientSky = float3(0.36, 0.38, 0.44);
    float3 ambientGround = float3(0.18, 0.17, 0.16);
    float3 ambientHemi = lerp(ambientGround, ambientSky, N.y * 0.5 + 0.5);
    float3 ambientDiffuse = ambientHemi * baseColor * (1.0 - metallic) * 0.85;

    // Subtle per-face normal directional tint for crisp CAD edge contrast
    float faceContrast = 1.0;
    if (useMaterialColor > 0.5) {
        faceContrast = 0.90 + 0.14 * N.y + 0.07 * N.x - 0.05 * N.z;
    }

    float3 finalColor = (directLighting * 3.14159265 + ambientDiffuse) * faceContrast +
                        envSpecular * (metallic * 0.85 + (1.0 - metallic) * 0.15);

    return float4(finalColor, alpha);
}
