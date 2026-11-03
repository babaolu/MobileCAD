struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float alpha : TEXCOORD0;
};

float4 main(FragmentInput input) : SV_Target {
    if (length(input.normal) < 0.01) {
        // Unlit CAD lines (grid, XYZ axes)
        return float4(input.color, input.alpha);
    }

    float3 norm = normalize(input.normal);

    // Studio CAD lighting: ambient + key directional light + fill light
    float ambient = 0.35;

    // Key light (warm upper-right)
    float3 keyLightDir = normalize(float3(0.6, 0.9, 0.7));
    float diff1 = max(dot(norm, keyLightDir), 0.0) * 0.50;

    // Fill light (cool lower-left)
    float3 fillLightDir = normalize(float3(-0.5, -0.3, -0.6));
    float diff2 = max(dot(norm, fillLightDir), 0.0) * 0.15;

    float lighting = ambient + diff1 + diff2;
    return float4(input.color * lighting, input.alpha);
}

