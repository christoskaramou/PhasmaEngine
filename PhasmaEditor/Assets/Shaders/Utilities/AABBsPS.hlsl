#include "../Common/Structures.hlsl"

PS_OUTPUT_Color mainPS(PS_INPUT_Color input)
{
    PS_OUTPUT_Color o;
    o.color = input.color;
    return o;
}
