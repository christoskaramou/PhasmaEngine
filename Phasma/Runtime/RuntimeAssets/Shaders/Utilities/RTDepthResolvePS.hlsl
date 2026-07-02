// Stamps the ray-traced primary-hit depth into the depth buffer, so depth-testing overlays
// (grid, lines, selection outline) work in full RT mode where no raster pass writes depth.
[[vk::binding(0, 0)]] Texture2D<float> RTDepth;

float mainPS(float4 pixelPos : SV_Position) : SV_Depth
{
    return RTDepth.Load(int3(pixelPos.xy, 0));
}
