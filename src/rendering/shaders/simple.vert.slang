struct PushConstants { row_major float4x4 mvp; };
[[vk::push_constant]] PushConstants pc;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
};

float3 getCubePos(uint id) {
    // 6 faces: +X, -X, +Y, -Y, +Z, -Z
    // 36 vertices total
    uint face = id / 6;
    uint vert = id % 6;
    // quad vertices: 0:(0,0), 1:(1,0), 2:(1,1), 3:(0,0), 4:(1,1), 5:(0,1)
    float u = (vert == 1 || vert == 2 || vert == 4) ? 0.5 : -0.5;
    float v = (vert == 2 || vert == 4 || vert == 5) ? 0.5 : -0.5;
    if (face == 0) return float3( 0.5,    v,   -u); // +X
    if (face == 1) return float3(-0.5,    v,    u); // -X
    if (face == 2) return float3(   u,  0.5,   -v); // +Y
    if (face == 3) return float3(   u, -0.5,    v); // -Y
    if (face == 4) return float3(   u,    v,  0.5); // +Z
    return float3( -u,    v, -0.5);                 // -Z
}

float3 getCubeColor(uint id) {
    uint face = id / 6;
    if (face == 0) return float3(0.92, 0.22, 0.22); // Red
    if (face == 1) return float3(0.95, 0.52, 0.12); // Orange
    if (face == 2) return float3(0.20, 0.82, 0.35); // Green
    if (face == 3) return float3(0.12, 0.78, 0.78); // Teal
    if (face == 4) return float3(0.22, 0.48, 0.95); // Blue
    return float3(0.95, 0.82, 0.18);                // Yellow
}

VertexOutput main(uint vertexID : SV_VertexID) {
    VertexOutput output;
    float3 pos = getCubePos(vertexID);
    output.position = mul(pc.mvp, float4(pos, 1.0));
    output.color = getCubeColor(vertexID);
    return output;
}
