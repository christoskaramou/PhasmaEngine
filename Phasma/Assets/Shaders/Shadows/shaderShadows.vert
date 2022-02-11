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

const int MAX_DATA_SIZE = 4096; // TODO: calculate on init
const int MAX_NUM_JOINTS = 128;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in ivec4 inJoint;
layout(location = 5) in vec4 inWeights;

layout(push_constant) uniform Constants {
    mat4 viewProjection;
    uint modelIndex;
    uint meshIndex;
    uint primitiveIndex;
} constants;

layout(set = 0, binding = 0) uniform UBO {
    mat4 data[MAX_DATA_SIZE];
} ubo;


#define modelMatrix ubo.data[constants.modelIndex]
#define modelMvp ubo.data[constants.modelIndex + 1]
#define modelPreviousMvp ubo.data[constants.modelIndex + 2]

#define meshMatrix ubo.data[constants.meshIndex]
#define meshPreviousMatrix ubo.data[constants.meshIndex + 1]
#define meshJointMatrix(x) ubo.data[constants.meshIndex + 2 + x]
// TEMP: joint matrices removed
#define meshJointCount 0 // ubo.data[constants.meshIndex + 3 + MAX_NUM_JOINTS][0][0];

void main() {
    mat4 boneTransform = mat4(1.0);
    if (meshJointCount > 0.0){
        boneTransform  =
        inWeights[0] * meshJointMatrix(inJoint[0]) +
        inWeights[1] * meshJointMatrix(inJoint[1]) +
        inWeights[2] * meshJointMatrix(inJoint[2]) +
        inWeights[3] * meshJointMatrix(inJoint[3]);
    }

    gl_Position = constants.viewProjection * modelMatrix * meshMatrix * boneTransform * vec4(inPosition, 1.0);
}
