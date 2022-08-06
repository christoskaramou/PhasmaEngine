struct VS_OUTPUT
{
    float2 uv : TEXCOORD0;
    float4 pos : SV_POSITION;
};

VS_OUTPUT mainVS(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.pos = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}
