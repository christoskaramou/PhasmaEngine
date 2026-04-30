// Displays the sorted uint32 buffer as a gridSize x gridSize cell grid, with
// each cell's brightness derived from its element value. Mirrors the WGSL
// reference bitonicDisplay.frag.wgsl (Elements display mode only).

struct DisplayUniforms
{
    uint gridSize;       // grid edge length in cells (e.g. 32 for 1024 elements)
    uint elementsPerRow; // same as gridSize for a square grid
};

[[vk::binding(0, 0)]] StructuredBuffer<uint> elements : register(t0, space0);
[[vk::binding(1, 0)]] cbuffer UBO                      : register(b1, space0)
{
    DisplayUniforms displayU;
}

struct PSInput
{
    float4 svPos                  : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float gridF = (float)displayU.gridSize;
    float2 cellF = float2(input.uv.x * gridF, input.uv.y * gridF);
    uint2 cell   = uint2((uint)floor(cellF.x), (uint)floor(cellF.y));

    // Clamp in case of rasterization edge cases.
    if (cell.x >= displayU.gridSize)
        cell.x = displayU.gridSize - 1u;
    if (cell.y >= displayU.gridSize)
        cell.y = displayU.gridSize - 1u;

    uint  elementIndex  = displayU.elementsPerRow * cell.y + cell.x;
    uint  totalElements = displayU.gridSize * displayU.gridSize;
    float normalized    = (float)elements[elementIndex] / (float)totalElements;
    float shade         = 1.0 - normalized;
    return float4(shade, shade, shade, 1.0);
}
