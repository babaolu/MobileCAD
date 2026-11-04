struct PushConstants {
    row_major float4x4 mvp;
    float4 normalRow0; // xyz: view-space normal matrix row 0, w: unused
    float4 normalRow1; // xyz: view-space normal matrix row 1, w: unused
    float4 normalRow2; // xyz: view-space normal matrix row 2, w: unused
    float4 pbrParams;  // x: metallic, y: roughness, z: useMaterialColor, w: alpha
};
[[vk::push_constant]] PushConstants pc;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float3 pbr : TEXCOORD1; // x: metallic, y: roughness, z: surfaceId
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float4 pbrParams : TEXCOORD0; // x: metallic, y: roughness, z: useMaterialColor, w: alpha
    [[vk::location(3)]] float3 viewPos : TEXCOORD1;
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    float4 clipPos = mul(pc.mvp, float4(input.position, 1.0));
    output.position = clipPos;

    // View-space position reconstructed from clip space (camera at origin looking down -Z)
    output.viewPos = float3(clipPos.x * 0.25, -clipPos.y * 0.45, -clipPos.w);

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

    // Color: read from input.color (which carries per-surface color)
    output.color = input.color;

    // PBR parameters:
    // x = per-vertex metallic (input.pbr.x)
    // y = per-vertex roughness (input.pbr.y)
    // z = useMaterialColor flag from push constant
    // w = alpha from push constant (1.0 for opaque solid, 0.38 for ghost cutter)
    output.pbrParams = float4(input.pbr.x, input.pbr.y, pc.pbrParams.z, pc.pbrParams.w);
    return output;
}
