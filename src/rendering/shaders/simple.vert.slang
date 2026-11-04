struct PushConstants {
    row_major float4x4 mvp;
    row_major float4x4 normalMatrix;
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
    [[vk::location(2)]] float3 pbrParams : TEXCOORD0;
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    if (length(input.normal) > 0.01) {
        output.normal = normalize(mul((float3x3)pc.normalMatrix, input.normal));
    } else {
        output.normal = float3(0.0, 0.0, 0.0);
    }

    if (pc.normalMatrix[3][2] > 0.5) {
        output.color = float3(pc.normalMatrix[0][3], pc.normalMatrix[1][3], pc.normalMatrix[2][3]);
    } else {
        output.color = input.color;
    }

    output.pbrParams = float3(pc.normalMatrix[3][0], pc.normalMatrix[3][1], pc.normalMatrix[3][3]);
    return output;
}
