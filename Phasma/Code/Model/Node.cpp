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
            return translate(mat4(1.0f), translation) * mat4(rotation) * glm::scale(mat4(1.0f), scale);
        case TransformationType::Identity:
        default:
            return mat4(1.f);
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
