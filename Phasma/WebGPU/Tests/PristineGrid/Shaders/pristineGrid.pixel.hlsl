struct GridArgs
{
    float4 lineColor;
    float4 baseColor;
    float2 lineWidth;
    float2 _pad;
};

[[vk::binding(0, 1)]] cbuffer GridArgsCB : register(b0, space1)
{
    GridArgs gridArgs;
}

struct PSInput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float2 fragUV : TEXCOORD0;
};

// Pristine Grid from Ben Golus's "Best Darn Grid" article:
// https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
float PristineGrid(float2 uv, float2 lineWidth)
{
    float2 ddx_uv = ddx(uv);
    float2 ddy_uv = ddy(uv);
    float2 uvDeriv = float2(length(float2(ddx_uv.x, ddy_uv.x)),
                            length(float2(ddx_uv.y, ddy_uv.y)));
    bool2 invertLine = lineWidth > float2(0.5, 0.5);
    float2 targetWidth = select(invertLine, 1.0 - lineWidth, lineWidth);
    float2 drawWidth = clamp(targetWidth, uvDeriv, float2(0.5, 0.5));
    float2 lineAA = uvDeriv * 1.5;
    float2 gridUV = abs(frac(uv) * 2.0 - 1.0);
    gridUV = select(invertLine, gridUV, 1.0 - gridUV);
    float2 grid2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);
    grid2 *= saturate(targetWidth / drawWidth);
    grid2 = lerp(grid2, targetWidth, saturate(uvDeriv * 2.0 - 1.0));
    grid2 = select(invertLine, 1.0 - grid2, grid2);
    return lerp(grid2.x, 1.0, grid2.y);
}

float4 PSMain(PSInput input) : SV_Target0
{
    float grid = PristineGrid(input.fragUV, gridArgs.lineWidth);
    return lerp(gridArgs.baseColor, gridArgs.lineColor, grid * gridArgs.lineColor.a);
}
