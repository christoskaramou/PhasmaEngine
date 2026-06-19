struct RenderUniforms
{
    column_major float4x4 modelMatrix;
    column_major float4x4 viewProjectionMatrix;
    float4 eyeTime;
};

struct SurfaceVertex
{
    float4 positionAO;
    float4 normalPad;
};

[[vk::binding(0, 0)]] cbuffer RenderUniformsCB : register(b0, space0)
{
    RenderUniforms uniforms;
};

[[vk::binding(1, 0)]] StructuredBuffer<SurfaceVertex> vertices : register(t0, space0);

struct VSOut
{
    float4 svPos : SV_Position;
    float3 positionWS : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float ao : TEXCOORD2;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    SurfaceVertex v = vertices[vertexId];
    float4 worldPos = mul(uniforms.modelMatrix, float4(v.positionAO.xyz, 1.0f));

    VSOut o;
    o.svPos = mul(uniforms.viewProjectionMatrix, worldPos);
    o.positionWS = worldPos.xyz;
    o.normalWS = normalize(mul(uniforms.modelMatrix, float4(v.normalPad.xyz, 0.0f)).xyz);
    o.ao = v.positionAO.w;
    return o;
}
