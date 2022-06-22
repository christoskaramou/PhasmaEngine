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

#include "Model/Node.h"
#include "Model/Mesh.h"
#include "Renderer/Buffer.h"
#include "Renderer/RHI.h"

namespace pe
{
    mat4 Node::LocalMatrix() const
    {
        switch (transformationType)
        {
        case TransformationType::Matrix:
            return matrix;
        case TransformationType::TRS:
            return transform(rotation, scale, translation);
        case TransformationType::Identity:
        default:
            return mat4::identity();
        }
    }

    mat4 Node::GetMatrix() const
    {
        mat4 m = LocalMatrix();
        Node *p = parent;
        while (p)
        {
            m = p->LocalMatrix() * m;
            p = p->parent;
        }
        return m;
    }

    void CalculateJointMatrixAsync(Mesh *mesh, Skin *skin, const mat4 &inverseTransform, const size_t index)
    {
        mesh->meshData.jointMatrices[index] =
            inverseTransform * skin->joints[index]->GetMatrix() * skin->inverseBindMatrices[index];
    }

    void Node::Update()
    {
        if (mesh)
        {
            auto &uniformBuffer = RHII.GetUniformBufferInfo(mesh->uniformBufferIndex);

            mesh->meshData.previousMatrix = mesh->meshData.matrix;
            mesh->meshData.matrix = GetMatrix();

            if (skin)
            {
                // Update joint matrices
                mat4 inverseTransform = inverse(mesh->meshData.matrix);
                const size_t numJoints = std::min(static_cast<uint32_t>(skin->joints.size()), MAX_NUM_JOINTS);

                // async calls should be at least bigger than a number, else this will be slower
                if (numJoints > 3)
                {
                    std::vector<std::future<void>> futures(numJoints);

                    for (size_t i = 0; i < numJoints; i++)
                        futures[i] = std::async(std::launch::async, CalculateJointMatrixAsync, mesh, skin,
                                                inverseTransform, i);

                    for (auto &f : futures)
                        f.get();
                }
                else
                {
                    for (size_t i = 0; i < numJoints; i++)
                        CalculateJointMatrixAsync(mesh, skin, inverseTransform, i);
                }

                MemoryRange mr{};
                mr.data = mesh->meshData.jointMatrices.data();
                mr.size = mesh->meshData.jointMatrices.size() * sizeof(mat4);
                mr.offset = RHII.GetFrameDynamicOffset(uniformBuffer.buffer->Size(), RHII.GetFrameIndex()) + mesh->uniformBufferOffset + 2 * sizeof(mat4);
                uniformBuffer.buffer->Copy(1, &mr, true);
            }

            MemoryRange mr{};
            mr.data = &mesh->meshData;
            mr.size = 2 * sizeof(mat4);
            mr.offset = RHII.GetFrameDynamicOffset(uniformBuffer.buffer->Size(), RHII.GetFrameIndex()) + mesh->uniformBufferOffset;
            uniformBuffer.buffer->Copy(1, &mr, true);
        }
    }
}
