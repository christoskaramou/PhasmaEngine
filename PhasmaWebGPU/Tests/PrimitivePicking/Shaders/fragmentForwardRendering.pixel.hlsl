// Forward pass pixel shader: simple N-dot-L shading, plus outputs the
// primitive index (+1 so 0 can mean "nothing picked") to a second color target.

struct Frame
{
    column_major float4x4 viewProjectionMatrix;
    column_major float4x4 invViewProjectionMatrix;
    float2 pickCoord;
    uint pickedPrimitive;
    uint _pad0;
};

ConstantBuffer<Frame> frame : register(b1);

struct PSInput
{
    float3 fragNormal : TEXCOORD0;
    uint primIndex : SV_PrimitiveID;
};

struct PSOutput
{
    float4 color : SV_Target0;
    uint primitive : SV_Target1;
};

PSOutput PSMain(PSInput input)
{
    float3 lightDirection = normalize(float3(4, 10, 6));
    float light = dot(normalize(input.fragNormal), lightDirection) * 0.5 + 0.5;
    float4 surfaceColor = float4(0.8, 0.8, 0.8, 1.0);

    PSOutput output;

    if (input.primIndex + 1 == frame.pickedPrimitive)
    {
        output.color = float4(1.0, 1.0, 0.0, 1.0);
    }
    else
    {
        output.color = float4(surfaceColor.xyz * light, surfaceColor.a);
    }

    output.primitive = input.primIndex + 1;
    return output;
}
