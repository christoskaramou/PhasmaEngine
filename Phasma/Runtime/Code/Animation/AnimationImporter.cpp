#include "AnimationImporter.h"
#include "API/Vertex.h"
#if defined(PE_ENABLE_ASSIMP)
#include <assimp/scene.h>
#endif

namespace pe
{
    namespace AnimationImporter
    {
#if defined(PE_ENABLE_ASSIMP)
        void ExtractBoneWeights(const aiMesh *mesh,
                                Skeleton &skeleton,
                                std::vector<Vertex> &vertices,
                                std::vector<PositionUvVertex> &posUvs)
        {
            if (!mesh || mesh->mNumBones == 0)
                return;

            struct Influence
            {
                int boneIndex;
                float weight;
            };
            std::vector<std::vector<Influence>> allInfluences(mesh->mNumVertices);

            for (unsigned int b = 0; b < mesh->mNumBones; b++)
            {
                const aiBone *bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                int boneIndex = skeleton.GetBoneIndex(boneName);
                if (boneIndex < 0)
                {
                    BoneInfo info;
                    info.name = boneName;
                    info.parentIndex = -1;
                    const aiMatrix4x4 &om = bone->mOffsetMatrix;
                    info.offsetMatrix = mat4(
                        om.a1, om.b1, om.c1, om.d1,
                        om.a2, om.b2, om.c2, om.d2,
                        om.a3, om.b3, om.c3, om.d3,
                        om.a4, om.b4, om.c4, om.d4);
                    boneIndex = static_cast<int>(skeleton.bones.size());
                    skeleton.bones.push_back(std::move(info));
                    skeleton.boneNameToIndex[boneName] = boneIndex;
                }

                for (unsigned int w = 0; w < bone->mNumWeights; w++)
                {
                    unsigned int vertId = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;
                    if (vertId >= mesh->mNumVertices || weight <= 0.0f)
                        continue;

                    allInfluences[vertId].push_back({boneIndex, weight});
                }
            }

            for (unsigned int v = 0; v < mesh->mNumVertices; v++)
            {
                auto &influences = allInfluences[v];
                if (influences.empty())
                    continue;

                std::sort(influences.begin(), influences.end(),
                          [](const Influence &a, const Influence &b)
                          { return a.weight > b.weight; });

                int count = std::min(static_cast<int>(influences.size()), 4);
                float totalWeight = 0.0f;
                for (int i = 0; i < count; i++)
                    totalWeight += influences[i].weight;

                float invTotal = (totalWeight > 0.0f) ? 1.0f / totalWeight : 0.0f;
                for (int i = 0; i < count; i++)
                {
                    uint32_t joint = static_cast<uint32_t>(influences[i].boneIndex);
                    float weight = influences[i].weight * invTotal;
                    vertices[v].joints[i] = joint;
                    vertices[v].weights[i] = weight;
                    posUvs[v].joints[i] = joint;
                    posUvs[v].weights[i] = weight;
                }
            }
        }

        void ResolveBoneParents(const aiScene *scene, Skeleton &skeleton)
        {
            if (!scene || skeleton.bones.empty())
                return;

            std::unordered_map<std::string, const aiNode *> nodeMap;
            std::function<void(const aiNode *)> buildMap = [&](const aiNode *node)
            {
                nodeMap[node->mName.C_Str()] = node;
                for (unsigned int c = 0; c < node->mNumChildren; c++)
                    buildMap(node->mChildren[c]);
            };
            buildMap(scene->mRootNode);

            auto toMat4 = [](const aiMatrix4x4 &m) -> mat4
            {
                return mat4(
                    m.a1, m.b1, m.c1, m.d1,
                    m.a2, m.b2, m.c2, m.d2,
                    m.a3, m.b3, m.c3, m.d3,
                    m.a4, m.b4, m.c4, m.d4);
            };

            for (int i = 0; i < static_cast<int>(skeleton.bones.size()); i++)
            {
                auto it = nodeMap.find(skeleton.bones[i].name);
                if (it == nodeMap.end())
                    continue;

                const aiNode *boneNode = it->second;

                mat4 intermediatePrefix(1.f);
                const aiNode *ancestor = boneNode->mParent;
                int parentBone = -1;
                while (ancestor)
                {
                    parentBone = skeleton.GetBoneIndex(ancestor->mName.C_Str());
                    if (parentBone >= 0)
                        break;
                    intermediatePrefix = toMat4(ancestor->mTransformation) * intermediatePrefix;
                    ancestor = ancestor->mParent;
                }
                skeleton.bones[i].intermediatePrefix = intermediatePrefix;
                skeleton.bones[i].localBindTransform = intermediatePrefix * toMat4(boneNode->mTransformation);
                skeleton.bones[i].parentIndex = parentBone;

                // The first root bone's intermediatePrefix captures the glTF root node
                // transform (e.g. Z-up → Y-up rotation).  Store it so the upload code
                // can strip it from joint matrices and let the shader's worldMatrix
                // apply it instead.
                if (parentBone < 0 && skeleton.rootTransform == mat4(1.f))
                    skeleton.rootTransform = intermediatePrefix;
            }
        }

        void ExtractAnimationClips(const aiScene *scene,
                                   const Skeleton &skeleton,
                                   std::vector<AnimationClip> &outClips)
        {
            if (!scene)
                return;

            for (unsigned int a = 0; a < scene->mNumAnimations; a++)
            {
                const aiAnimation *aiAnim = scene->mAnimations[a];
                AnimationClip clip;
                clip.name = aiAnim->mName.length > 0 ? aiAnim->mName.C_Str() : ("Animation_" + std::to_string(a));
                clip.duration = static_cast<float>(aiAnim->mDuration);
                clip.ticksPerSecond = aiAnim->mTicksPerSecond > 0.0 ? static_cast<float>(aiAnim->mTicksPerSecond) : 25.0f;

                for (unsigned int ch = 0; ch < aiAnim->mNumChannels; ch++)
                {
                    const aiNodeAnim *aiChannel = aiAnim->mChannels[ch];
                    int boneIndex = skeleton.GetBoneIndex(aiChannel->mNodeName.C_Str());
                    if (boneIndex < 0)
                        continue;

                    AnimationChannel channel;
                    channel.boneIndex = boneIndex;

                    channel.positionKeys.reserve(aiChannel->mNumPositionKeys);
                    for (unsigned int k = 0; k < aiChannel->mNumPositionKeys; k++)
                    {
                        const auto &key = aiChannel->mPositionKeys[k];
                        channel.positionKeys.push_back({static_cast<float>(key.mTime),
                                                        vec3(key.mValue.x, key.mValue.y, key.mValue.z)});
                    }

                    channel.rotationKeys.reserve(aiChannel->mNumRotationKeys);
                    for (unsigned int k = 0; k < aiChannel->mNumRotationKeys; k++)
                    {
                        const auto &key = aiChannel->mRotationKeys[k];
                        channel.rotationKeys.push_back({static_cast<float>(key.mTime),
                                                        quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z)});
                    }

                    channel.scaleKeys.reserve(aiChannel->mNumScalingKeys);
                    for (unsigned int k = 0; k < aiChannel->mNumScalingKeys; k++)
                    {
                        const auto &key = aiChannel->mScalingKeys[k];
                        channel.scaleKeys.push_back({static_cast<float>(key.mTime),
                                                     vec3(key.mValue.x, key.mValue.y, key.mValue.z)});
                    }

                    clip.channels.push_back(std::move(channel));
                }

                outClips.push_back(std::move(clip));
            }

            if (!skeleton.bones.empty())
                PE_INFO("[Assimp] Loaded %d bones, %d animation clips", (int)skeleton.bones.size(), (int)outClips.size());
        }
#else
        void ExtractBoneWeights(const aiMesh *,
                                Skeleton &,
                                std::vector<Vertex> &,
                                std::vector<PositionUvVertex> &)
        {
        }

        void ResolveBoneParents(const aiScene *, Skeleton &)
        {
        }

        void ExtractAnimationClips(const aiScene *,
                                   const Skeleton &,
                                   std::vector<AnimationClip> &)
        {
        }
#endif
    } // namespace AnimationImporter
} // namespace pe
