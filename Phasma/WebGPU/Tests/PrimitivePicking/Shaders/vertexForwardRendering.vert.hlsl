// Forward pass vertex shader: transforms teapot vertices and passes the world
// space normal to the pixel stage.

struct Model
{
    column_major float4x4 modelMatrix;
    column_major float4x4 normalModelMatrix;
};

struct Frame
{
    column_major float4x4 viewProjectionMatrix;
    column_major float4x4 invViewProjectionMatrix;
    float2 pickCoord;
    uint pickedPrimitive;
    uint _pad0;
};

ConstantBuffer<Model> uniforms : register(b0);
ConstantBuffer<Frame> frame : register(b1);

struct VSInput
{
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 fragNormal : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float3 worldPosition = mul(uniforms.modelMatrix, float4(input.position, 1.0)).xyz;
    output.position = mul(frame.viewProjectionMatrix, float4(worldPosition, 1.0));
    output.fragNormal = normalize(mul(uniforms.normalModelMatrix, float4(input.normal, 1.0)).xyz);
    return output;
}
