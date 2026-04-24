// Port of gpu-life/src/render.wgsl (fragment stage). Round splat with
// smoothstep alpha edge + class-index-driven RGB lookup from colours[].

[[vk::binding(2, 0)]] StructuredBuffer<float> colours : register(t2, space0);

struct PSIn
{
    float4 svPos : SV_Position;
    float2 uv : TEXCOORD0;
    float colour : TEXCOORD1;
};

float4 PSMain(PSIn input) : SV_Target0
{
    float d = length(input.uv);
    if (d > 1.0)
        discard;

    float delta = fwidth(d);
    float alpha = 1.0 - smoothstep(1.0 - delta, 1.0, d);

    uint c = (uint)input.colour;
    float r = colours[c * 3u + 0u];
    float g = colours[c * 3u + 1u];
    float b = colours[c * 3u + 2u];

    return float4(r, g, b, alpha);
}
