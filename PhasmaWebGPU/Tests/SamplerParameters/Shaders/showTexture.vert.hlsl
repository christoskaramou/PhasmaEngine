struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texelCoord : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint mipLevel : TEXCOORD1;
};

static const uint kMipLevels = 4;

VSOutput VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    float2 square[6] = {
        float2(0.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 0.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 1.0),
    };

    float2 uv = square[vid];
    uint mipLevel = iid;
    float mipSize = float(1u << (kMipLevels - mipLevel));

    VSOutput o;
    o.svPos = float4(uv * 2.0 - float2(1.0, 1.0), 0.0, 1.0);
    o.texelCoord = uv * mipSize;
    o.mipLevel = mipLevel;
    return o;
}
