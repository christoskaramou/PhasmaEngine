/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../DepthOfField/DOF.glsl"

layout(push_constant) uniform Constants { vec4 values; } constants;

#if BINDLESS_DESCRIPTORS
    layout (set = 0, binding = 0) uniform sampler2D samplers[];
    layout (set = 1, binding = 0) uniform Uniforms {vec4 data;} uniforms[];
    layout (set = 2, binding = 0) buffer Storages {vec4 data;} storages[];
    #define sampler(index) samplers[nonuniformEXT(index)]
    #define uniform(index) uniforms[nonuniformEXT(index)].data
    #define storage(index) storages[nonuniformEXT(index)].data
    #define sampler_color sampler(0)
    #define sampler_depth sampler(1)
#else
    layout (set = 0, binding = 0) uniform sampler2D sampler_color;
    layout (set = 0, binding = 1) uniform sampler2D sampler_depth;
#endif

layout (location = 0) in vec2 in_UV;
layout (location = 0) out vec4 outColor;

void main()
{
    outColor.xyz = depthOfField(sampler_color, sampler_depth, in_UV, constants.values.x, constants.values.y);
    outColor.w = texture(sampler_color, in_UV).w;
    //outColor.xyz = uniform(0).xyz;
    //outColor.xyz = storage(0).xyz;
}
