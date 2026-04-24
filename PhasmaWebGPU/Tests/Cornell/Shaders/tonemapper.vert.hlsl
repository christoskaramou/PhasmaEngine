// Full-screen triangle vertex shader for the tonemap render pass.
// Deviates from upstream (which uses compute + storage write into swap);
// a fragment pipeline is portable and does not require surface storage usage.
// The upstream HDR framebuffer is already oriented top-left = (0,0), which
// matches Vulkan SV_Position, so no Y flip is needed here.

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 pos = float2((vid << 1) & 2, vid & 2);
    o.uv = pos;
    o.pos = float4(pos * 2.0 - 1.0, 0.0, 1.0);
    return o;
}
