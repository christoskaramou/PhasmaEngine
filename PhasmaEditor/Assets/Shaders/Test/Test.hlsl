#include "../Common/Structures.hlsl"

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;
    output.vColor = float4(input.UV, 0.5f, 1.0f);
    return output;
}
