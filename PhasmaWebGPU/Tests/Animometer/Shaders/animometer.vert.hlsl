struct Time
{
    float value;
};

struct Uniforms
{
    float scale;
    float offsetX;
    float offsetY;
    float scalar;
    float scalarOffset;
};

[[vk::binding(0, 0)]] cbuffer TimeCB : register(b0, space0)        { Time time; }
[[vk::binding(0, 1)]] StructuredBuffer<Uniforms> triangles : register(t0, space1);

struct VSInput
{
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float4 color    : COLOR;
};

struct VSOutput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float4 vColor : TEXCOORD0;
};

VSOutput VSMain(VSInput input, uint iid : SV_InstanceID)
{
    Uniforms uniforms = triangles[iid];
    float fade = frac(uniforms.scalarOffset + time.value * uniforms.scalar / 10.0f);
    if (fade < 0.5f)
        fade = fade * 2.0f;
    else
        fade = (1.0f - fade) * 2.0f;

    float xpos  = input.position.x * uniforms.scale;
    float ypos  = input.position.y * uniforms.scale;
    float angle = 3.14159f * 2.0f * fade;
    float xrot  = xpos * cos(angle) - ypos * sin(angle);
    float yrot  = xpos * sin(angle) + ypos * cos(angle);
    xpos = xrot + uniforms.offsetX;
    ypos = yrot + uniforms.offsetY;

    VSOutput o;
    o.vColor = float4(fade, 1.0f - fade, 0.0f, 1.0f) + input.color;
    // Flip Y for Vulkan Y-down NDC (WebGPU NDC is Y-up).
    o.svPos  = float4(xpos, -ypos, 0.0f, 1.0f);
    return o;
}
