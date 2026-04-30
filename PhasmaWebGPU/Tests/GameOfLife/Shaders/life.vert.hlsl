struct Size
{
    uint2 wh;
};

[[vk::binding(0, 0)]] cbuffer UBO : register(b0, space0)
{
    Size size;
}

struct VSInput
{
    [[vk::location(0)]] uint cell : TEXCOORD0;
    [[vk::location(1)]] uint2 pos : TEXCOORD1;
    uint instanceId               : SV_InstanceID;
};

struct VSOutput
{
    float4 svPos                     : SV_Position;
    [[vk::location(0)]] float cell   : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    uint w = size.wh.x;
    uint h = size.wh.y;
    uint i = input.instanceId;
    float mx = (float)max(w, h);
    float x = ((float)(i % w + input.pos.x) / (float)w - 0.5f) * 2.f * (float)w / mx;
    float y = ((float)((i - (i % w)) / w + input.pos.y) / (float)h - 0.5f) * 2.f * (float)h / mx;

    VSOutput o;
    o.svPos = float4(x, y, 0.f, 1.f);
    o.cell = (float)input.cell;
    return o;
}
