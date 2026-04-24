// Port of webgpu-samples/alpha-to-coverage-emulator scenes/CrossingGradients.wgsl
// (fragment side). Not used by the default (Foliage) preset but ported for
// content completeness. Native A2C path: return the interpolated vertex color.

struct Varying
{
    float4 pos   : SV_Position;
    float4 color : COLOR0;
};

float4 ComputeFragment(Varying vary)
{
    return vary.color;
}

float4 PSMain(Varying vary) : SV_Target0
{
    return ComputeFragment(vary);
}
