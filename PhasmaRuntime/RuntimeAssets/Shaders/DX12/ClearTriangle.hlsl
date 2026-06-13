struct VSInput
{
    float4 position : POSITION0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VSOutput mainVS(VSInput input)
{
    VSOutput output;
    output.position = input.position;
    output.color = float4(0.95f, 0.62f, 0.20f, 1.0f);
    return output;
}

float4 mainPS(VSOutput input) : SV_Target0
{
    return input.color;
}
