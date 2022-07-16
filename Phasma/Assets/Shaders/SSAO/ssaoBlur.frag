#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout (set = 0, binding = 0) uniform sampler2D samplerSSAO;

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outColor;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(samplerSSAO, 0));
    float result = 0.0;
    for (int x = -2; x < 2; x++)
    {
        for (int y = -2; y < 2; y++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(samplerSSAO, inUV + offset).r;
        }
    }
    outColor = result / 16.0;
}
