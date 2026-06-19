// MSDF text pixel shader — 1:1 port of msdfText.wgsl fragmentMain.
// Antialiasing technique from Paul Houx:
// https://github.com/Chlumsky/msdfgen/issues/22#issuecomment-234958005

[[vk::binding(0, 0)]] Texture2D fontTexture : register(t0, space0);
[[vk::binding(1, 0)]] SamplerState fontSampler : register(s1, space0);

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
    [[vk::location(1)]] float4 textColor : TEXCOORD1;
};

float SampleMsdf(float2 uv)
{
    float4 c = fontTexture.Sample(fontSampler, uv);
    return max(min(c.r, c.g), min(max(c.r, c.g), c.b));
}

float4 PSMain(PSInput input) : SV_Target0
{
    // pxRange (AKA distanceRange) comes from the msdfgen tool.
    // Don McCurdy's tool uses the default value of 4.
    const float pxRange = 4.0f;

    float width;
    float height;
    fontTexture.GetDimensions(width, height);
    float2 sz = float2(width, height);

    float dx = sz.x * length(float2(ddx(input.texcoord.x), ddy(input.texcoord.x)));
    float dy = sz.y * length(float2(ddx(input.texcoord.y), ddy(input.texcoord.y)));
    float toPixels = pxRange * rsqrt(dx * dx + dy * dy);

    float sigDist = SampleMsdf(input.texcoord) - 0.5f;
    float pxDist = sigDist * toPixels;

    const float edgeWidth = 0.5f;
    float alpha = smoothstep(-edgeWidth, edgeWidth, pxDist);

    if (alpha < 0.001f)
        discard;

    return float4(input.textColor.rgb, input.textColor.a * alpha);
}
