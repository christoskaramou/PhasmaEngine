#pragma once
#include "API/Vertex.h"
#include "Animation/AnimationTypes.h"

namespace pe
{
    struct Vertex;
    struct AabbVertex;
    class Material;
    class Sampler;
    class CommandBuffer;
    class Image;

    struct MeshInfo
    {
        uint32_t vertexOffset = 0, verticesCount = 0; // offset and count in used vertex buffer
        uint32_t indexOffset = 0, indicesCount = 0;   // offset and count in used index buffer
        uint32_t positionsOffset = 0;
        size_t aabbVertexOffset = 0;
        uint32_t aabbColor = 0;
        AABB boundingBox;
        RenderType renderType;

        // First-class material reference (shared across meshes with same material)
        Material *material = nullptr;
        bool skinned = false;
    };

    struct NodeInfo
    {
        int parent = -1;
        std::vector<int> children;
        mat4 localMatrix;

        std::string name;
    };

    class ModelAsset : public Resource
    {
    public:
        ModelAsset();
        virtual ~ModelAsset();

        virtual void Load() override {}
        virtual void Unload() override;

        // Factory method to load models based on file extension
        static ModelAsset *Load(const std::filesystem::path &file);
        static void DestroyDefaults();

        // Common interface methods
        int CreateNode(const std::string &name, int parentIndex = -1, const mat4 &localMatrix = mat4(1.f), int meshIndex = -1);
        void ReparentNode(int nodeIndex, int newParentIndex);

        // Removes a node and its subtree. Returns true if the model is now empty (caller should delete it).
        bool RemoveNode(int nodeIndex);

        ResourceHandle<Image> LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath);

        // Embedded textures (e.g. inside a .glb) have no source file on disk. The cook calls this to
        // recover the ORIGINAL encoded bytes (PNG/JPG) so it can write them next to the .pemesh and
        // store a portable relative path. The base model has no embedded source and returns false;
        // ModelAssetAssimp overrides it to read from the retained aiScene.
        virtual bool GetEmbeddedTextureBytes(const std::string &textureName,
                                             std::vector<uint8_t> &outBytes,
                                             std::string &outExtension) const
        {
            (void)textureName;
            (void)outBytes;
            (void)outExtension;
            return false;
        }

        // Getters
        size_t GetId() const { return m_id; }

        const std::vector<Vertex> &GetVertices() const { return m_vertices; }
        const std::vector<PositionUvVertex> &GetPositionUvs() const { return m_positionUvs; }
        const std::vector<AabbVertex> &GetAabbVertices() const { return m_aabbVertices; }
        const std::vector<uint32_t> &GetIndices() const { return m_indices; }

        const std::vector<ResourceHandle<Image>> &GetImages() const { return m_images; }
        const std::vector<Sampler *> &GetSamplers() const { return m_samplers; }
        std::vector<std::unique_ptr<Material>> &GetOwnedMaterials() { return m_materials; }
        const std::vector<std::unique_ptr<Material>> &GetOwnedMaterials() const { return m_materials; }

        const std::vector<MeshInfo> &GetMeshInfos() const { return m_meshInfos; }
        const std::vector<NodeInfo> &GetNodeInfos() const { return m_nodeInfos; }

        mat4 &GetMatrix() { return m_matrix; }
        const mat4 &GetMatrix() const { return m_matrix; }
        void SetMatrix(const mat4 &matrix) { m_matrix = matrix; }

        uint32_t GetVerticesCount() const { return m_verticesCount; }
        uint32_t GetIndicesCount() const { return m_indicesCount; }
        uint32_t GetMeshCount() const { return m_meshCount; }
        int GetRootNodeIndex() const;

        const std::string &GetLabel() const { return m_label; }
        const std::filesystem::path &GetFilePath() const { return m_filePath; }
        void SetLabel(const std::string &label) { m_label = label; }
        const std::string &GetPrimitiveType() const { return m_primitiveType; }
        void SetPrimitiveType(const std::string &type) { m_primitiveType = type; }
        const vec4 &GetPrimitiveParams() const { return m_primitiveParams; }
        uint32_t GetPrimitiveParamCount() const { return m_primitiveParamCount; }
        void SetPrimitiveParams(const vec4 &params, uint32_t count)
        {
            m_primitiveParams = params;
            m_primitiveParamCount = count;
        }
        bool IsPrimitive() const { return !m_primitiveType.empty(); }
        void SetNodeName(int nodeIndex, const std::string &name);
        const std::string &GetNodeName(int nodeIndex) const;
        NodeInfo *GetNodeInfo(int nodeIndex);
        const NodeInfo *GetNodeInfo(int nodeIndex) const;
        const mat4 &GetNodeLocalMatrix(int nodeIndex) const;
        void SetNodeLocalMatrix(int nodeIndex, const mat4 &localMatrix);
        int GetNodeParentIndex(int nodeIndex) const;
        void SetNodeParentIndex(int nodeIndex, int parentIndex);
        void RebuildNodeChildrenFromParents();
        AABB GetNodeWorldBoundingBox(int nodeIndex) const;

        int GetNodeCount() const { return static_cast<int>(m_nodeInfos.size()); }
        int GetMeshInfoCount() const { return static_cast<int>(m_meshInfos.size()); }
        MeshInfo *GetMeshInfo(int meshIndex);
        const MeshInfo *GetMeshInfo(int meshIndex) const;
        int GetNodeMesh(int nodeIndex) const;

        bool HasAnimations() const { return !m_animations.empty(); }
        bool HasSkeleton() const { return !m_skeleton.bones.empty(); }
        const Skeleton &GetSkeleton() const { return m_skeleton; }
        const std::vector<AnimationClip> &GetAnimations() const { return m_animations; }
        std::vector<AnimationClip> &GetMutableAnimations() { return m_animations; }
        int GetJointCount() const { return m_skeleton.GetBoneCount(); }

        struct DefaultResources
        {
            Image *black = nullptr;
            Image *normal = nullptr;
            Image *white = nullptr;
            Sampler *sampler = nullptr;

            void EnsureCreated(CommandBuffer *cmd);
        };

        static DefaultResources &GetDefaultResources(CommandBuffer *cmd);
        static const DefaultResources &GetDefaultResources();
        static DefaultResources &Defaults();

    protected:
        friend class Scene;
        friend class Primitives;
        friend class ModelAssetCooked;

        static constexpr uint32_t TextureBit(TextureType type) { return 1u << static_cast<uint32_t>(type); }
        mat4 ComputeNodeWorldMatrix(int nodeIndex) const;

        void ResetResources(CommandBuffer *cmd);

        // Resource ownership helpers:
        // - owned resources are destroyed in ~ModelAsset()
        // - shared resources are NOT destroyed (e.g. default textures/sampler)
        void AddImage(Image *image, bool owned);
        void AddSampler(Sampler *sampler, bool owned);

        size_t m_id;

        std::vector<ResourceHandle<Image>> m_images{};
        std::vector<Sampler *> m_samplers{};

        std::unordered_set<Sampler *> m_sharedSamplers{};

        std::vector<MeshInfo> m_meshInfos{};

        // Owned materials created during import (shared across meshes via MeshInfo::material pointer)
        std::vector<std::unique_ptr<Material>> m_materials{};
        std::vector<NodeInfo> m_nodeInfos{};
        std::vector<int> m_nodeToMesh{};

        std::vector<Vertex> m_vertices;
        std::vector<PositionUvVertex> m_positionUvs;
        std::vector<AabbVertex> m_aabbVertices;
        std::vector<uint32_t> m_indices;

        mat4 m_matrix = mat4(1.f);

        uint32_t m_verticesCount = 0;
        uint32_t m_indicesCount = 0;
        uint32_t m_meshCount = 0;

        std::string m_label;
        std::string m_primitiveType; // "cube", "sphere", "plane", etc. Empty for file-loaded models.
        vec4 m_primitiveParams = vec4(0.f);
        uint32_t m_primitiveParamCount = 0;
        std::filesystem::path m_filePath;

        Skeleton m_skeleton;
        std::vector<AnimationClip> m_animations;
    };
} // namespace pe
