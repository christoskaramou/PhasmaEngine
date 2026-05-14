struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput mainVS(uint vertexID : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0),
    };

    const float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0),
    };

    VSOutput output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.uv = uvs[vertexID];
    return output;
}

Texture2D<float4> Source : register(t0, space0);
SamplerState sampler_Source : register(s0, space0);

cbuffer BlitConstants : register(b0, space0)
{
    float4 scaleOffset;
};

float4 mainPS(VSOutput input) : SV_Target0
{
    const float2 uv = input.uv * scaleOffset.xy + scaleOffset.zw;
    return Source.SampleLevel(sampler_Source, uv, 0.0);
}
