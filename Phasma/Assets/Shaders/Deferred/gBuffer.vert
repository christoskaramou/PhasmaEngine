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
#extension GL_EXT_nonuniform_qualifier : require

const int MAX_NUM_JOINTS = 128;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 matrix;
    mat4 previousMatrix;
    mat4 jointMatrix[MAX_NUM_JOINTS];
    float jointCount;
    float dummy[3];
} uboMesh;

layout(set = 1, binding = 5) uniform UniformBufferObject2 {
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    float occlusionlMetalRoughness;
    float hasBones;
    float dummy[3];
} uboPrimitive;

layout(set = 2, binding = 0) uniform UniformBufferObject3 {
    mat4 matrix;
    mat4 mvp;
    mat4 previousMvp;
} uboModel;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in ivec4 inJoint;
layout(location = 5) in vec4 inWeights;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec4 outColor;
layout (location = 3) out vec4 baseColorFactor;
layout (location = 4) out vec3 emissiveFactor;
layout (location = 5) out vec4 metRoughAlphacutOcl;
layout (location = 6) out vec4 positionCS;
layout (location = 7) out vec4 previousPositionCS;
layout (location = 8) out vec4 positionWS;

void main()
{
    mat4 boneTransform = mat4(1.0);
    if (uboMesh.jointCount > 0.0){
        boneTransform  =
        inWeights[0] * uboMesh.jointMatrix[inJoint[0]] +
        inWeights[1] * uboMesh.jointMatrix[inJoint[1]] +
        inWeights[2] * uboMesh.jointMatrix[inJoint[2]] +
        inWeights[3] * uboMesh.jointMatrix[inJoint[3]];
    }

    vec4 inPos = vec4(inPosition, 1.0f);

    mat3 mNormal = transpose(inverse(mat3(uboModel.matrix * uboMesh.matrix * boneTransform)));

    // UV
    outUV = inTexCoords;

    // Normal in world space
    outNormal = normalize(mNormal * inNormal);

    // Color
    outColor = inColor;

    // Factors
    baseColorFactor = uboPrimitive.baseColorFactor;
    emissiveFactor = uboPrimitive.emissiveFactor.xyz;
    metRoughAlphacutOcl = vec4(uboPrimitive.metallicFactor, uboPrimitive.roughnessFactor, uboPrimitive.alphaCutoff, uboPrimitive.occlusionlMetalRoughness);

    positionWS = uboModel.matrix * uboMesh.matrix * boneTransform * inPos;

    // Velocity
    positionCS = uboModel.mvp * uboMesh.matrix * boneTransform * inPos;// clip space
    previousPositionCS = uboModel.previousMvp * uboMesh.previousMatrix * boneTransform * inPos;// clip space

    gl_Position = positionCS;
}
