struct FragmentInput {
    float3 color : COLOR;
};

float4 main(FragmentInput input) : SV_Target {
    return float4(input.color, 1.0);
}
