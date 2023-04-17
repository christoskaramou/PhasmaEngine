struct PS_INPUT
{
    float3 color : COLOR;
};

struct PS_OUTPUT
{
    float4 color : SV_TARGET;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT o;
    o.color = float4(input.color, 1.0f);
    return o;
}
