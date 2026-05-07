struct VertexInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}
