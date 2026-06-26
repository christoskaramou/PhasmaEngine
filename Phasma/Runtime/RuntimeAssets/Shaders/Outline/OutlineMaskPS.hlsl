#include "../Common/Structures.hlsl"

// Selected silhouettes -> R8 coverage mask. X-ray: no depth test, solid fill (alpha-cut meshes
// outline as their full quad, not the cutout — good enough for an editor selection outline).
float4 mainPS(PS_INPUT_Position_Uv_ID input) : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 0.0);
}
