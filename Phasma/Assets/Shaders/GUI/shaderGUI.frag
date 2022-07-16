#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 fColor;

layout(set=0, binding=0) uniform sampler2D sTexture;

layout(location = 0) in struct{
    vec4 Color;
    vec2 UV;
} In;

void main()
{
    fColor = In.Color * texture(sTexture, In.UV.st);
}
