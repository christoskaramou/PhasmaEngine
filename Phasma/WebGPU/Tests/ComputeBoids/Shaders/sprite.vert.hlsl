struct VSInput
{
    [[vk::location(0)]] float2 a_particlePos : POSITION0;
    [[vk::location(1)]] float2 a_particleVel : POSITION1;
    [[vk::location(2)]] float2 a_pos         : POSITION2;
};

struct VSOutput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float4 color  : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    float angle = -atan2(input.a_particleVel.x, input.a_particleVel.y);
    float2 pos = float2(
        (input.a_pos.x * cos(angle)) - (input.a_pos.y * sin(angle)),
        (input.a_pos.x * sin(angle)) + (input.a_pos.y * cos(angle)));

    float2 world = pos + input.a_particlePos;

    VSOutput o;
    // Flip Y for Vulkan Y-down NDC.
    o.svPos = float4(world.x, -world.y, 0.0f, 1.0f);
    o.color = float4(
        1.0f - sin(angle + 1.0f) - input.a_particleVel.y,
        pos.x * 100.0f - input.a_particleVel.y + 0.1f,
        input.a_particleVel.x + cos(angle + 0.5f),
        1.0f);
    return o;
}
