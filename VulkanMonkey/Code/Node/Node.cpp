#include "Node.h"

using namespace vm;

mat4 Node::localMatrix()
{
	switch (transformationType)
	{
	case TRANSFORMATION_MATRIX: 
		return matrix;
	case TRANSFORMATION_TRS:
		return translate(mat4(1.0f), translation) * rotation.matrix() * vm::scale(mat4(1.0f), scale);
	case TRANSFORMATION_IDENTITY:
	default:
		return mat4::identity();
	}
}

mat4 Node::getMatrix()
{
	mat4 m = localMatrix();
	Node *p = parent;
	while (p) {
		m = p->localMatrix() * m;
		p = p->parent;
	}
	return m;
}

void Node::update(Camera& camera)
{
	if (mesh) {
		mesh->ubo.previousMatrix = mesh->ubo.matrix;
		mesh->ubo.matrix = getMatrix();
		mesh->ubo.view = camera.view;
		mesh->ubo.projection = camera.projection;

		for (auto& primitive : mesh->primitives) {
			vec4 bs = mesh->ubo.matrix * vec4(vec3(primitive.boundingSphere), 1.0f);
			bs.w = primitive.boundingSphere.w * mesh->ubo.matrix[0][0]; // scale 
			primitive.cull = !camera.SphereInFrustum(bs);
		}

		if (skin) {
			// TODO: these calculations are heavy
			// TODO: use primitive uniforms, so calculations are done only once
			// TODO: size vs calculation speed :(

			// Update join matrices
			mat4 inverseTransform = inverse(mesh->ubo.matrix);
			size_t numJoints = std::min((uint32_t)skin->joints.size(), MAX_NUM_JOINTS);
			for (size_t i = 0; i < numJoints; i++) {
				Node *jointNode = skin->joints[i];
				mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
				jointMat = inverseTransform * jointMat;
				mesh->ubo.jointMatrix[i] = jointMat;
			}
			mesh->ubo.jointcount = (float)numJoints;
			memcpy(mesh->uniformBuffer.data, &mesh->ubo, sizeof(mesh->ubo));
		}
		else {
			memcpy(mesh->uniformBuffer.data, &mesh->ubo, 4 * sizeof(mat4));
		}
	}

	//for (auto& child : children) {
	//	child->update(camera);
	//}
}