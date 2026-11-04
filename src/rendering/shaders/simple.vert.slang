struct PushConstants {
    row_major float4x4 mvp;
    float4 normalRow0; // xyz: view-space normal matrix row 0, w: matColor.r
    float4 normalRow1; // xyz: view-space normal matrix row 1, w: matColor.g
    float4 normalRow2; // xyz: view-space normal matrix row 2, w: matColor.b
    float4 pbrParams;  // x: metallic, y: roughness, z: useMaterialColor, w: alpha
};
[[vk::push_constant]] PushConstants pc;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float4 pbrParams : TEXCOORD0;
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    if (length(input.normal) > 0.01) {
        float3x3 normMat = float3x3(
            pc.normalRow0.xyz,
            pc.normalRow1.xyz,
            pc.normalRow2.xyz
        );
        output.normal = normalize(mul(normMat, input.normal));
    } else {
        output.normal = float3(0.0, 0.0, 0.0);
    }

    if (pc.pbrParams.z > 0.5) {
        output.color = float3(pc.normalRow0.w, pc.normalRow1.w, pc.normalRow2.w);
    } else {
        output.color = input.color;
    }

    output.pbrParams = pc.pbrParams;
    return output;
}
