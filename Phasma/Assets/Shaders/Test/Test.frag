#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(push_constant) uniform Constants { vec4 values; } constants;

layout (set = 0, binding = 0) uniform sampler2D samplers[];
layout (set = 1, binding = 0) uniform Uniforms {vec4 data;} uniforms[];
layout (set = 2, binding = 0) buffer Storages {vec4 data;} storages[];

#define sampler(index) samplers[nonuniformEXT(index)]
#define uniform(index) uniforms[nonuniformEXT(index)].data
#define storage(index) storages[nonuniformEXT(index)].data

#define sampler_color sampler(0)

layout (location = 0) in vec2 in_UV;
layout (location = 0) out vec4 outColor;

void main()
{
    outColor = texture(sampler_color, in_UV).grba;
    //outColor.xyz = uniform(0).xyz;
    //outColor.xyz = storage(0).xyz;
}
