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

#include "Node.h"
#include "Model/Mesh.h"
#include "Queue.h"

namespace pe
{
	mat4 Node::localMatrix() const
	{
		switch (transformationType)
		{
			case TRANSFORMATION_MATRIX:
				return matrix;
			case TRANSFORMATION_TRS:
			{
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
	
	void calculateMeshJointMatrix(Mesh* mesh, Skin* skin, const mat4& inverseTransform, const size_t index)
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
						futures[i] = std::async(
								std::launch::async, calculateMeshJointMatrix, mesh, skin, inverseTransform, i
						);
					
					for (auto& f : futures)
						f.get();
				}
				else
				{
					for (size_t i = 0; i < numJoints; i++)
						calculateMeshJointMatrix(mesh, skin, inverseTransform, i);
				}
				
				mesh->ubo.jointcount = static_cast<float>(numJoints);
				mesh->uniformBuffer->CopyRequest<Launch::AsyncDeferred>({ &mesh->ubo, sizeof(mesh->ubo), 0 });
			}
			else
			{
				mesh->uniformBuffer->CopyRequest<Launch::AsyncDeferred>({ &mesh->ubo, 2 * sizeof(mat4), 0 });
			}
		}
	}
}
