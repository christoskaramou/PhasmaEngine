#pragma once

#include "Animation/AnimationTypes.h"

struct aiScene;
struct aiMesh;

namespace pe
{
    struct Vertex;
    struct PositionUvVertex;

    namespace AnimationImporter
    {
        void ExtractBoneWeights(const aiMesh *mesh,
                                Skeleton &skeleton,
                                std::vector<Vertex> &vertices,
                                std::vector<PositionUvVertex> &posUvs);

        void ResolveBoneParents(const aiScene *scene, Skeleton &skeleton);

        void ExtractAnimationClips(const aiScene *scene,
                                   const Skeleton &skeleton,
                                   std::vector<AnimationClip> &outClips);
    } // namespace AnimationImporter
} // namespace pe
