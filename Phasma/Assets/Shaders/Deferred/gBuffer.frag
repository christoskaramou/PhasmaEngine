#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../Common/common.glsl"

layout(push_constant) uniform Constants {
    vec2 projJitter;
    vec2 prevProjJitter;
    uint modelIndex;
    uint meshIndex;
    uint meshJointCount;
    uint primitiveIndex;
    uint primitiveImageIndex;
} constants;

layout(set = 1, binding = 0) uniform sampler material_sampler;
layout(set = 1, binding = 1) uniform texture2D textures[];

#define sampler(index) sampler2D(textures[nonuniformEXT(index)], material_sampler)

#define bcSampler sampler(constants.primitiveImageIndex + 0) // BaseColor
#define mrSampler sampler(constants.primitiveImageIndex + 1) // MetallicRoughness
#define nSampler sampler(constants.primitiveImageIndex + 2) // Normal
#define oSampler sampler(constants.primitiveImageIndex + 3) // Occlusion
#define eSampler sampler(constants.primitiveImageIndex + 4) // Emissive

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;
layout (location = 3) in vec4 baseColorFactor;
layout (location = 4) in vec3 emissiveFactor;
layout (location = 5) in vec4 metRoughAlphacutOcl;
layout (location = 6) in vec4 positionCS;
layout (location = 7) in vec4 previousPositionCS;
layout (location = 8) in vec4 positionWS;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec4 outAlbedo;
layout (location = 2) out vec3 outMetRough;
layout (location = 3) out vec2 outVelocity;
layout (location = 4) out vec4 outEmissive;

void main() {
    vec4 basicColor = texture(bcSampler, inUV) + inColor;
    if (basicColor.a < metRoughAlphacutOcl.z) discard; // needed because alpha blending is messed up when objects are not in order
    vec3 metRough = texture(mrSampler, inUV).xyz;
    vec3 emissive = texture(eSampler, inUV).xyz;
    float ao = texture(oSampler, inUV).r;
    vec3 tangentNormal = texture(nSampler, inUV).xyz;
    
    outNormal = GetNormal(positionWS.xyz, tangentNormal, inNormal, inUV) * vec3(0.5) + vec3(0.5);
    outAlbedo = vec4(basicColor.xyz * ao, basicColor.a) * baseColorFactor;
    outMetRough = vec3(0.0, metRough.y, metRough.z);
    vec2 cancelJitter = constants.prevProjJitter - constants.projJitter;
    outVelocity = (previousPositionCS.xy / previousPositionCS.w - positionCS.xy / positionCS.w) - cancelJitter;
    outVelocity *= vec2(0.5f, 0.5f);
    outEmissive = vec4(emissive * emissiveFactor, 0.0);
}
