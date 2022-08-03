struct VS_INPUT
{
    float3 vPos   : POSITION;
    float3 vColor : COLOR0;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;  // interpolated vertex position (system value)
    float4 Color    : COLOR0;       // interpolated diffuse color
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    output.Position = 0.0f;
    output.Color = 0.0f;
    return output;
}

//float4 mainPS(float2 input) : SV_TARGET
//{
//    return float4(input, 0.0f, 1.0f);
//}