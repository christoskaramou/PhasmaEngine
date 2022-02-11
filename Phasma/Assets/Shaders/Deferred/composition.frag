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

#include "Light.glsl"

layout (location = 0) in vec2 in_UV;
layout (location = 0) out vec4 outColor;

void main()
{
    float depth = texture(sampler_depth, in_UV).x;

    // if the depth is maximum it hits the skybox
    if (depth == 0.0)
    {
        // Skybox
        vec3 wolrdPos = GetPosFromUV(in_UV, depth, screenSpace.invViewProj);
        vec3 samplePos = normalize(wolrdPos - ubo.camPos.xyz);
        //samplePos.xy *= -1.0f; // the final image is blitted to the swapchain with -x and -y
        outColor = vec4(texture(sampler_cube_map, samplePos).xyz, 1.0);

        // Fog
        // screenSpace.effects3.y -> fog on/off
        // screenSpace.effects2.w -> fog max height
        // screenSpace.effects2.x -> fog global thickness
        // screenSpace.effects3.x -> fog ground thickness
        if (screenSpace.effects3.y > 0.5)
        {

            float fogNear = 0.5;
            float fogFar = 1000.0;
            float depth = length(wolrdPos - ubo.camPos.xyz);
            float groundHeightMin = -100.0;
            float groundHeightMax = screenSpace.effects2.w * 20.0;
            float globalThickness = 1.0;
            float groundThickness = 2.0;

            float depthFactor = clamp(1.0 - (fogFar - depth) / (fogFar - fogNear), 0.0, 1.0);
            float heightFactor = clamp(1.0 - (wolrdPos.y - groundHeightMin) / (groundHeightMax - groundHeightMin), 0.0, 1.0);
            float globalFactor = depthFactor * globalThickness;
            float noiseFactor = 1.0;// texture(NoiseMap, position.xz).x;
            float groundFactor = heightFactor * depthFactor * noiseFactor * groundThickness;
            float fogFactor = clamp(groundFactor + globalFactor, 0.0, 1.0);

            outColor.xyz = mix(outColor.xyz, vec3(1.0), fogFactor);

            // Volumetric light
            if (screenSpace.effects1.y > 0.5)
            outColor.xyz += VolumetricLighting(ubo.sun, wolrdPos, in_UV, sun.cascades[1], fogFactor);
        }

        return;
    }

    vec3 wolrdPos = GetPosFromUV(in_UV, depth, screenSpace.invViewProj);
    vec3 normal = texture(sampler_normal, in_UV).xyz;
    vec3 metRough = texture(sampler_met_rough, in_UV).xyz;
    vec4 albedo = texture(sampler_albedo, in_UV);

    Material material;
    material.albedo = albedo.xyz;
    material.roughness = metRough.y;
    material.metallic = metRough.z;
    material.F0 = mix(vec3(0.04f), material.albedo, material.metallic);

    // Ambient
    float occlusion = screenSpace.effects0.x > 0.5 ? texture(sampler_ssao_blur, in_UV).x : 1.0;
    float skyLight = clamp(ubo.sun.color.a, 0.025f, 1.0f);
    skyLight *= screenSpace.effects3.z > 0.5 ? 0.25 : 0.15;
    float ambientLight = skyLight * occlusion;
    vec3 fragColor = vec3(0.0);// 0.1 * material.albedo.xyz;

    // screenSpace.effects3.z -> shadow cast
    if (screenSpace.effects3.z > 0.5)
    {
        float shadow = CalculateShadows(wolrdPos, length(wolrdPos - ubo.camPos.xyz), dot(normal, ubo.sun.direction.xyz));
        fragColor += DirectLight(material, wolrdPos, ubo.camPos.xyz, normal, occlusion, shadow);
    }

    // IBL
    IBL ibl;
    ibl.reflectivity = vec3(0.5);
    if (screenSpace.effects1.x > 0.5) {
        ibl = ImageBasedLighting(material, normal, normalize(wolrdPos - ubo.camPos.xyz), sampler_cube_map, sampler_lut_IBL);
        fragColor += ibl.final_color * ambientLight;
    }

    for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
        fragColor += ComputePointLight(i, material, wolrdPos, ubo.camPos.xyz, normal, occlusion);

    outColor = vec4(fragColor, albedo.a) + texture(sampler_emission, in_UV);

    // SSR
    if (screenSpace.effects0.y > 0.5) {
        vec3 ssr = texture(sampler_ssr, in_UV).xyz * ibl.reflectivity;
        outColor += vec4(ssr, 0.0);// * (1.0 - material.roughness);
    }

    // Tone Mapping
    if (screenSpace.effects0.z > 0.5)
    outColor.xyz = Reinhard(outColor.xyz);

    // Fog
    if (screenSpace.effects3.y > 0.5) {
        // screenSpace.effects3.y -> fog on/off
        // screenSpace.effects2.w -> fog max height
        // screenSpace.effects2.x -> fog global thickness
        // screenSpace.effects3.x -> fog ground thickness

        float fogNear = 0.5;
        float fogFar = 1000.0;
        float depth = length(wolrdPos - ubo.camPos.xyz);
        float groundHeightMin = -2.0;
        float groundHeightMax = screenSpace.effects2.w;
        float globalThickness = screenSpace.effects2.x;
        float groundThickness = screenSpace.effects3.x;

        float depthFactor = clamp(1.0 - (fogFar - depth) / (fogFar - fogNear), 0.0, 1.0);
        float heightFactor = clamp(1.0 - (wolrdPos.y - groundHeightMin) / (groundHeightMax - groundHeightMin), 0.0, 1.0);
        float globalFactor = depthFactor * globalThickness;
        float noiseFactor = 1.0;// texture(NoiseMap, position.xz).x;
        float groundFactor = heightFactor * depthFactor * noiseFactor * groundThickness;
        float fogFactor = clamp(groundFactor + globalFactor, 0.0, 1.0);

        outColor.xyz = mix(outColor.xyz, vec3(1.0), fogFactor);

        // Volumetric light
        if (screenSpace.effects1.y > 0.5)
        outColor.xyz += VolumetricLighting(ubo.sun, wolrdPos, in_UV, sun.cascades[1], fogFactor);
    }
}
