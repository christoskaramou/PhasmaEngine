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

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in ivec4 inJoint;
layout(location = 5) in vec4 inWeights;

layout(push_constant) uniform Constants { mat4 viewProjection; } pushConst;

layout(set = 0, binding = 0) uniform UniformBuffer1 {
    mat4 matrix;
    mat4 previousMatrix;
    mat4 jointMatrix[MAX_NUM_JOINTS];
    float jointCount;
    float dummy[3];
}mesh;

layout(set = 1, binding = 0) uniform UniformBuffer2 {
    mat4 matrix;
    mat4 dummy[3];
}model;

void main() {
    mat4 boneTransform = mat4(1.0);
    if (mesh.jointCount > 0.0){
        boneTransform  =
        inWeights[0] * mesh.jointMatrix[inJoint[0]] +
        inWeights[1] * mesh.jointMatrix[inJoint[1]] +
        inWeights[2] * mesh.jointMatrix[inJoint[2]] +
        inWeights[3] * mesh.jointMatrix[inJoint[3]];
    }

    gl_Position = pushConst.viewProjection * model.matrix * mesh.matrix * boneTransform * vec4(inPosition, 1.0);
}
