#include "../Common/Structures.hlsl"

PS_OUTPUT_Color mainPS(PS_INPUT_Color input)
{
    PS_OUTPUT_Color o;
    o.color = float4(input.color, 1.0f);
    return o;
}
