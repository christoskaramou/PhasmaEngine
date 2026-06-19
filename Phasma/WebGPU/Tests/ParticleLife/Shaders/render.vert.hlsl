// Port of gpu-life/src/render.wgsl (vertex stage).
// Six-vertex triangle-strip quad per particle instance, instance-data-driven
// position/velocity/colour. Upstream outputs directly in NDC so we apply
// Vulkan Y-flip here (svPos.y = -y).

struct Uniforms
{
    float aspect;
    float _pad0;
    float _pad1;
    float _pad2;
    float4 mouse; // .x/.y = world-space cursor, .z = down, .w = type
    float pointSize;
};

struct CameraData
{
    float2 pos;
    float zoom;
    float _pad;
};

[[vk::binding(0, 0)]] cbuffer UniformsCB : register(b0, space0)
{
    Uniforms uniforms;
};
[[vk::binding(1, 0)]] cbuffer CameraCB : register(b1, space0)
{
    CameraData cam;
};

struct VSIn
{
    float2 pos : POSITION;    // location 0
    float2 vel : TEXCOORD0;   // location 1 (unused in VS)
    float colour : TEXCOORD1; // location 2
    uint vertexId : SV_VertexID;
};

struct VSOut
{
    float4 svPos : SV_Position;
    float2 uv : TEXCOORD0;
    float colour : TEXCOORD1;
};

VSOut VSMain(VSIn input)
{
    // Upstream quad layout (triangle-strip of 6 vertices used as triangle-list).
    static const float2 kPositions[6] = {
        float2(-1.0, -1.0),
        float2(1.0, -1.0),
        float2(-1.0, 1.0),
        float2(-1.0, 1.0),
        float2(1.0, -1.0),
        float2(1.0, 1.0),
    };

    float2 q = kPositions[input.vertexId];
    float size = max(uniforms.pointSize, 0.005 / cam.zoom);
    float2 offset = q * size;
    float2 worldPos = input.pos + offset;

    float a = uniforms.aspect;

    VSOut output;
    float x = (worldPos.x - cam.pos.x) * cam.zoom / a;
    float y = (worldPos.y - cam.pos.y) * cam.zoom;

    // Vulkan clip-space Y is flipped relative to WebGPU. Upstream writes
    // directly to NDC, so mirror Y here to match visually.
    output.svPos = float4(x, -y, 0.0, 1.0);
    output.uv = q;
    output.colour = input.colour;
    return output;
}
