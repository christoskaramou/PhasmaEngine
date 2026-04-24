// Debug-view pixel shader: visualises each primitive index as a distinct color.

Texture2D<uint> primitiveTex : register(t0);

float4 PSMain(float4 coord : SV_Position) : SV_Target0
{
    uint primitiveIndex = primitiveTex.Load(int3(int2(floor(coord.xy)), 0)).x;
    float4 result;
    result.r = float(primitiveIndex % 8) / 8.0;
    result.g = float((primitiveIndex / 8) % 8) / 8.0;
    result.b = float((primitiveIndex / 64) % 8) / 8.0;
    result.a = 1.0;
    return result;
}
