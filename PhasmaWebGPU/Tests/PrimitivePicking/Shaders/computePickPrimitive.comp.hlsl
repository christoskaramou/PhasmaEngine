// Picking compute shader: loads the primitive index at the cursor pixel out of
// the r32uint primitive-id texture and writes it into the pickedPrimitive field
// of the shared Frame buffer.
//
// Frame layout (matches the CPU + rendering shader cbuffer):
//   [  0, 64)  viewProjectionMatrix (float4x4)
//   [ 64,128)  invViewProjectionMatrix (float4x4)
//   [128,136)  pickCoord (float2)
//   [136,140)  pickedPrimitive (uint)

RWByteAddressBuffer frame : register(u0);
Texture2D<uint> primitiveTex : register(t1);

static const uint kPickCoordOffset = 128;
static const uint kPickedPrimitiveOffset = 136;

[numthreads(1, 1, 1)] void CSMain()
{
    float2 pickCoordF = asfloat(frame.Load2(kPickCoordOffset));
    uint2 texel = uint2(pickCoordF);
    uint picked = primitiveTex.Load(int3(int2(texel), 0)).x;
    frame.Store(kPickedPrimitiveOffset, picked);
}
