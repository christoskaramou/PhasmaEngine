struct Uniforms
{
    column_major float4x4 modelViewProjectionMatrix;
    uint     maxStorableFragments;
    uint     targetWidth;
    uint     _pad0;
    uint     _pad1;
};

[[vk::binding(0, 0)]] cbuffer UniformsCB : register(b0, space0) { Uniforms uniforms; };

struct VSIn
{
    float3 position : POSITION;
    uint   instance : SV_InstanceID;
};

struct VSOut
{
    float4          position : SV_Position;
    nointerpolation uint     instance : INSTANCE;
};

VSOut VSMain(VSIn input)
{
    // Staggered 4x2 grid, offset so translucent teapots overlap their opaque neighbours.
    const float gridWidth = 125.0;
    const float cellSize  = gridWidth / 4.0;

    uint row = input.instance / 2u;
    uint col = input.instance % 2u;

    float xOffset = -gridWidth * 0.5 + cellSize * 0.5
                  + 2.0 * cellSize * (float)col
                  + (((row % 2u) == 0u) ? cellSize : 0.0);

    float zOffset = -gridWidth * 0.5 + cellSize * 0.5 + 2.0
                  + (float)row * cellSize;

    float4 offsetPos = float4(input.position.x + xOffset,
                              input.position.y,
                              input.position.z + zOffset,
                              1.0);

    VSOut output;
    output.position = mul(uniforms.modelViewProjectionMatrix, offsetPos);
    output.instance = input.instance;
    return output;
}
