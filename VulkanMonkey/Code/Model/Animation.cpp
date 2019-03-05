#include "Animation.h"
#include <assert.h>
#include "../Math/Math.h"

using namespace vm;

void Animation::loadBones(const aiMesh* pMesh, uint32_t vertexOffset, std::vector<VertexBoneData>& bones)
{
	for (uint32_t i = 0; i < pMesh->mNumBones; i++) {
		uint32_t boneIndex = 0;
		std::string boneName(pMesh->mBones[i]->mName.data);

		if (boneMapping.find(boneName) == boneMapping.end()) {
			boneIndex = numBones;
			numBones++;
			BoneInfo bi;
			boneInfo.push_back(bi);
		}
		else {
			boneIndex = boneMapping[boneName];
		}

		boneMapping[boneName] = boneIndex;
		boneInfo[boneIndex].boneOffset = pMesh->mBones[i]->mOffsetMatrix;

		for (uint32_t j = 0; j < pMesh->mBones[i]->mNumWeights; j++) {
			uint32_t vertexID = vertexOffset + pMesh->mBones[i]->mWeights[j].mVertexId;
			float weight = pMesh->mBones[i]->mWeights[j].mWeight;
			bones[vertexID].add(boneIndex, weight);
		}
	}
	boneTransforms.resize(numBones);
}

void Animation::setAnimation(uint32_t animationIndex)
{
	if (!scene->HasAnimations()) return;
	assert(animationIndex < scene->mNumAnimations);
	pAnimation = scene->mAnimations[animationIndex];
}

void Animation::bonesTransform(float delta)
{
	if (!scene->HasAnimations()) return;
	runningTimeSeconds += delta;
	float TicksPerSecond = scene->mAnimations[0]->mTicksPerSecond != 0 ? (float)scene->mAnimations[0]->mTicksPerSecond : 25.0f;
	float TimeInTicks = runningTimeSeconds * TicksPerSecond;
	float AnimationTime = fmod(TimeInTicks, (float)scene->mAnimations[0]->mDuration);

	aiMatrix4x4 identity = aiMatrix4x4();
	readNodeHierarchy(AnimationTime, scene->mRootNode, identity);

	for (uint32_t i = 0; i < boneTransforms.size(); i++)
		boneTransforms[i] = boneInfo[i].finalTransformation;
}

const aiNodeAnim* Animation::findNodeAnim(const aiAnimation * animation, const std::string nodeName)
{
	for (uint32_t i = 0; i < animation->mNumChannels; i++)
	{
		const aiNodeAnim* nodeAnim = animation->mChannels[i];
		if (std::string(nodeAnim->mNodeName.data) == nodeName) // too slow!!!!!!!!
		{
			return nodeAnim;
		}
	}
	return nullptr;
}

aiMatrix4x4 Animation::interpolateTranslation(float time, const aiNodeAnim * pNodeAnim)
{
	aiVector3D translation;

	if (pNodeAnim->mNumPositionKeys == 1)
	{
		translation = pNodeAnim->mPositionKeys[0].mValue;
	}
	else
	{
		uint32_t frameIndex = 0;
		for (uint32_t i = 0; i < pNodeAnim->mNumPositionKeys - 1; i++)
		{
			if (time < (float)pNodeAnim->mPositionKeys[i + 1].mTime)
			{
				frameIndex = i;
				break;
			}
		}

		aiVectorKey currentFrame = pNodeAnim->mPositionKeys[frameIndex];
		aiVectorKey nextFrame = pNodeAnim->mPositionKeys[(frameIndex + 1) % pNodeAnim->mNumPositionKeys];

		float delta = (time - (float)currentFrame.mTime) / (float)(nextFrame.mTime - currentFrame.mTime);

		const aiVector3D& start = currentFrame.mValue;
		const aiVector3D& end = nextFrame.mValue;

		translation = (delta * (end - start)) + start;
	}

	aiMatrix4x4 mat;
	aiMatrix4x4::Translation(translation, mat);
	return mat;
}

aiMatrix4x4 Animation::interpolateRotation(float time, const aiNodeAnim * pNodeAnim)
{
	aiQuaternion rotation;

	if (pNodeAnim->mNumRotationKeys == 1)
	{
		rotation = pNodeAnim->mRotationKeys[0].mValue;
	}
	else
	{
		uint32_t frameIndex = 0;
		for (uint32_t i = 0; i < pNodeAnim->mNumRotationKeys - 1; i++)
		{
			if (time < (float)pNodeAnim->mRotationKeys[i + 1].mTime)
			{
				frameIndex = i;
				break;
			}
		}

		aiQuatKey currentFrame = pNodeAnim->mRotationKeys[frameIndex];
		aiQuatKey nextFrame = pNodeAnim->mRotationKeys[(frameIndex + 1) % pNodeAnim->mNumRotationKeys];

		float delta = (time - (float)currentFrame.mTime) / (float)(nextFrame.mTime - currentFrame.mTime);

		const aiQuaternion& start = currentFrame.mValue;
		const aiQuaternion& end = nextFrame.mValue;

		aiQuaternion::Interpolate(rotation, start, end, delta);
		rotation.Normalize();
	}

	aiMatrix4x4 mat(rotation.GetMatrix());
	return mat;
}

aiMatrix4x4 Animation::interpolateScale(float time, const aiNodeAnim * pNodeAnim)
{
	aiVector3D scale;

	if (pNodeAnim->mNumScalingKeys == 1)
	{
		scale = pNodeAnim->mScalingKeys[0].mValue;
	}
	else
	{
		uint32_t frameIndex = 0;
		for (uint32_t i = 0; i < pNodeAnim->mNumScalingKeys - 1; i++)
		{
			if (time < (float)pNodeAnim->mScalingKeys[i + 1].mTime)
			{
				frameIndex = i;
				break;
			}
		}

		aiVectorKey currentFrame = pNodeAnim->mScalingKeys[frameIndex];
		aiVectorKey nextFrame = pNodeAnim->mScalingKeys[(frameIndex + 1) % pNodeAnim->mNumScalingKeys];

		float delta = (time - (float)currentFrame.mTime) / (float)(nextFrame.mTime - currentFrame.mTime);

		const aiVector3D& start = currentFrame.mValue;
		const aiVector3D& end = nextFrame.mValue;

		scale = (delta * (end - start)) + start;
	}

	aiMatrix4x4 mat;
	aiMatrix4x4::Scaling(scale, mat);
	return mat;
}

void Animation::readNodeHierarchy(float AnimationTime, const aiNode * pNode, const aiMatrix4x4 & ParentTransform)
{
	std::string NodeName(pNode->mName.data);

	aiMatrix4x4 NodeTransformation(pNode->mTransformation);

	const aiNodeAnim* pNodeAnim = findNodeAnim(pAnimation, NodeName);

	if (pNodeAnim)
	{
		// Get interpolated matrices between current and next frame
		aiMatrix4x4 matScale = interpolateScale(AnimationTime, pNodeAnim);
		aiMatrix4x4 matRotation = interpolateRotation(AnimationTime, pNodeAnim);
		aiMatrix4x4 matTranslation = interpolateTranslation(AnimationTime, pNodeAnim);

		NodeTransformation = matTranslation * matRotation * matScale;
	}

	aiMatrix4x4 GlobalTransformation = ParentTransform * NodeTransformation;

	if (boneMapping.find(NodeName) != boneMapping.end())
	{
		uint32_t BoneIndex = boneMapping[NodeName];
		boneInfo[BoneIndex].finalTransformation = globalInverseTransform * GlobalTransformation * boneInfo[BoneIndex].boneOffset;
	}

	for (uint32_t i = 0; i < pNode->mNumChildren; i++)
	{
		readNodeHierarchy(AnimationTime, pNode->mChildren[i], GlobalTransformation);
	}
}