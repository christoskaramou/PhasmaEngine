// Stamps the ray-traced primary-hit depth into the depth buffer, so depth-testing overlays
// (grid, lines, selection outline) work in full RT mode where no raster pass writes depth.
#include "../Common/Structures.hlsl"

[[vk::binding(0, 0)]] Texture2D<float> RTDepth;

// Take the full VS_OUTPUT_Position_Uv that Quad.hlsl mainVS emits (uv + SV_Position), even though only
// the position is used. DXIL requires the PS input signature to match the VS output register layout; a
// position-only input packs SV_Position to a different register and the DX12 PSO fails to link with
// "SV_Position defined for mismatched hardware registers" (Vulkan/SPIR-V tolerates the mismatch).
float mainPS(VS_OUTPUT_Position_Uv input) : SV_Depth
{
    return RTDepth.Load(int3(input.position.xy, 0));
}
