#include "Node.h"
#include <future>
#include "../Model/Mesh.h"
#include "Queue.h"

using namespace vm;

mat4 Node::localMatrix() const
{
	switch (transformationType)
	{
	case TRANSFORMATION_MATRIX: 
		return matrix;
	case TRANSFORMATION_TRS: {
		return transform(rotation, scale, translation);
	}
	case TRANSFORMATION_IDENTITY:
	default:
		return mat4::identity();
	}
}

mat4 Node::getMatrix() const
{
	mat4 m = localMatrix();
	Node* p = parent;
	while (p)
	{
		m = p->localMatrix() * m;
		p = p->parent;
	}
	return m;
}

void calculateMeshJointMatrixAsync(Mesh* mesh, Skin* skin, const mat4& inverseTransform, const size_t index)
{
	mesh->ubo.jointMatrix[index] = inverseTransform * skin->joints[index]->getMatrix() * skin->inverseBindMatrices[index];
}

void Node::update()
{
	if (mesh)
	{
		mesh->ubo.previousMatrix = mesh->ubo.matrix;
		mesh->ubo.matrix = getMatrix();

		if (skin)
		{
			// Update joint matrices
			mat4 inverseTransform = inverse(mesh->ubo.matrix);
			const size_t numJoints = std::min(static_cast<uint32_t>(skin->joints.size()), MAX_NUM_JOINTS);

			// async calls should be at least bigger than a number, else this will be slower
			if (numJoints > 3)
			{
				std::vector<std::future<void>> futures(numJoints);

				for (size_t i = 0; i < numJoints; i++)
					futures[i] = std::async(std::launch::async, calculateMeshJointMatrixAsync, mesh, skin, inverseTransform, i);

				for (auto& f : futures)
					f.get();
			}
			else
			{
				for (size_t i = 0; i < numJoints; i++)
					calculateMeshJointMatrixAsync(mesh, skin, inverseTransform, i);
			}

			mesh->ubo.jointcount = static_cast<float>(numJoints);
			Queue::memcpyRequest(&mesh->uniformBuffer, { { &mesh->ubo, sizeof(mesh->ubo), 0} });
			//mesh->uniformBuffer.map();
			//memcpy(mesh->uniformBuffer.data, &mesh->ubo, sizeof(mesh->ubo));
			//mesh->uniformBuffer.flush();
			//mesh->uniformBuffer.unmap();
		}
		else
		{
			Queue::memcpyRequest(&mesh->uniformBuffer, { { &mesh->ubo, 2 * sizeof(mat4), 0} });
			//mesh->uniformBuffer.map();
			//memcpy(mesh->uniformBuffer.data, &mesh->ubo, 2 * sizeof(mat4));
			//mesh->uniformBuffer.flush();
			//mesh->uniformBuffer.unmap();
		}
	}
}