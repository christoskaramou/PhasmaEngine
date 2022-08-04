struct PS_INPUT
{
    float2 UV : TEXCOORD0;
};

struct PS_OUTPUT
{
    float4 vColor : SV_Target0;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    output.vColor = float4(input.UV, 0.5f, 1.0f);
    return output;
}