struct PushConstants {
    row_major float4x4 mvp;
    row_major float4x4 model;
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
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    if (length(input.normal) > 0.01) {
        output.normal = normalize(mul((float3x3)pc.model, input.normal));
    } else {
        output.normal = float3(0.0, 0.0, 0.0);
    }
    output.color = input.color;
    return output;
}

