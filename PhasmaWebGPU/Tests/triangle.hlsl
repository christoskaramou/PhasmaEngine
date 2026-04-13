cbuffer UBO : register(b0)
{
    float4x4 mvp;
};

struct VSInput
{
    float2 position : POSITION;
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(mvp, float4(input.position, 0.0, 1.0));
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    return float4(input.color, 1.0);
}
