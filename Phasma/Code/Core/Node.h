#pragma once

#include "Math.h"
#include "../Camera/Camera.h"

namespace pe
{
	class Mesh;
	class Node;

	struct Skin
	{
		std::string name;
		Node* skeletonRoot;
		std::vector<mat4> inverseBindMatrices;
		std::vector<Node*> joints;
	};

	// It is invalid to have both 'matrix' and any of 'translation'/'rotation'/'scale'
	//   spec: "A node can have either a 'matrix' or any combination of 'translation'/'rotation'/'scale' (TRS) properties"
	enum TransformationType
	{
		TRANSFORMATION_IDENTITY = 0,
		TRANSFORMATION_MATRIX,
		TRANSFORMATION_TRS
	};

	class Node
	{
	public:
		// These ids will help to store nodes linearly in storage buffers so they can be manipulated in compute shaders by id
		// TODO: figure how to handle the nodes in a buffer when they are removed
		inline static uint32_t s_id = 0;
		const uint32_t id = s_id++;
		Node* parent;
		uint32_t index;
		std::vector<Node*> children;
		mat4 matrix;
		std::string name;
		Mesh* mesh;
		Skin* skin;
		int32_t skinIndex = -1;
		vec3 translation;
		vec3 scale;
		quat rotation;
		TransformationType transformationType;

		mat4 localMatrix() const;

		mat4 getMatrix() const;

		void update();
	};
}
