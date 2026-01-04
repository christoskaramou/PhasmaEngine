#include "../Common/Structures.hlsl"

PS_OUTPUT_Particle mainPS(PS_INPUT_Particle input)
{
    PS_OUTPUT_Particle output;
    output.color = input.color;
    return output;
}
