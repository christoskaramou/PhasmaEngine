#pragma once

#include "../../include/assimp/Importer.hpp"      // C++ importer interface
#include "../../include/assimp/scene.h"           // Output data structure
#include "../../include/assimp/postprocess.h"     // Post processing flags
#include "../../include/assimp/DefaultLogger.hpp"
#include "../../include/assimp/pbrmaterial.h"
#include <map>
#include <array>
#include <algorithm>

namespace vm {
	// Maximum number of bones per mesh
	// Must not be higher than same const in skinning shader
	constexpr auto MAX_BONES = 110;
	// Maximum number of bones per vertex
	constexpr auto MAX_BONES_PER_VERTEX = 4;

	// Skinned mesh class

	// Per-vertex bone IDs and weights
	struct VertexBoneData
	{
		std::array<uint32_t, MAX_BONES_PER_VERTEX> IDs;
		std::array<float, MAX_BONES_PER_VERTEX> weights;

		// Ad bone weighting to vertex info
		void add(uint32_t boneID, float weight)
		{
			auto i = std::distance(weights.begin(), std::min_element(weights.begin(), weights.end()));
			IDs[i] = boneID;
			weights[i] = weight;
			
			//for (uint32_t i = 0; i < MAX_BONES_PER_VERTEX; i++)
			//{
			//	if (weights[i] == 0.0f)
			//	{
			//		IDs[i] = boneID;
			//		weights[i] = weight;
			//		return;
			//	}
			//}

			//exit(-24);
		}
	};

	// Stores information on a single bone
	struct BoneInfo
	{
		aiMatrix4x4 boneOffset;
		aiMatrix4x4 finalTransformation;

		BoneInfo()
		{
			boneOffset = aiMatrix4x4();
			finalTransformation = aiMatrix4x4();
		};
	};

	struct Animation
	{
		const aiScene* scene;
		// Bone related stuff
		// Maps bone name with index
		std::map<std::string, uint32_t> boneMapping;
		// Bone details
		std::vector<BoneInfo> boneInfo;
		// Number of bones present
		uint32_t numBones = 0;
		// Root inverese transform matrix
		aiMatrix4x4 globalInverseTransform;
		// Per-vertex bone info
		std::vector<VertexBoneData> bones;
		// Bone transformations
		std::vector<aiMatrix4x4> boneTransforms;

		float runningTimeSeconds = 0.0f;

		// Modifier for the animation 
		float animationSpeed = 0.75f;
		// Currently active animation
		aiAnimation* pAnimation;
		// Number of animations
		uint32_t numAnimations = 0;

		// Load bone information from ASSIMP mesh
		void loadBones(const aiMesh* pMesh, uint32_t vertexOffset, std::vector<VertexBoneData>& Bones);
		// Set active animation by index
		void setAnimation(uint32_t animationIndex);
		// Recursive bone transformation for given animation time
		void bonesTransform(float delta);
		// Find animation for a given node
		const aiNodeAnim* findNodeAnim(const aiAnimation* animation, const std::string nodeName);
		// Returns a 4x4 matrix with interpolated translation between current and next frame
		aiMatrix4x4 interpolateTranslation(float time, const aiNodeAnim* pNodeAnim);
		// Returns a 4x4 matrix with interpolated rotation between current and next frame
		aiMatrix4x4 interpolateRotation(float time, const aiNodeAnim* pNodeAnim);
		// Returns a 4x4 matrix with interpolated scaling between current and next frame
		aiMatrix4x4 interpolateScale(float time, const aiNodeAnim* pNodeAnim);
		// Get node hierarchy for current animation time
		void readNodeHierarchy(float AnimationTime, const aiNode* pNode, const aiMatrix4x4& ParentTransform);
	};
}