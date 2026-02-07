#pragma once

namespace pe
{
    class AccelerationStructure;
    class Buffer;
    class Camera;
    class CommandBuffer;
    class Image;
    class ImageView;
    class Model;
    class Sampler;

    struct DrawInfo
    {
        Model *model;
        int node;
        float distance;
    };

    class ParticleManager;

    class Scene
    {
    public:
        Scene();
        ~Scene();

        void Update();
        void UpdateGeometryBuffers();
        void UpdateTextures();
        void UploadBuffers(CommandBuffer *cmd);
        void UpdateTLASTransformations(CommandBuffer *cmd); // Update instance transforms and rebuild TLAS
        void AddModel(Model *model);
        void RemoveModel(Model *model);
        Camera *AddCamera();

        ParticleManager *GetParticleManager() { return m_particleManager; }
        Camera *GetActiveCamera() { return m_cameras.at(0); }
        Camera *GetCamera(int index) const { return m_cameras.at(index); }
        OrderedMap<size_t, Model *> &GetModels() { return m_models; }
        const OrderedMap<size_t, Model *> &GetModels() const { return m_models; }

        bool HasOpaqueDrawInfo() const { return !m_drawInfosOpaque.empty() || !m_drawInfosAlphaCut.empty(); }
        bool HasAlphaDrawInfo() const { return !m_drawInfosAlphaBlend.empty() || !m_drawInfosTransmission.empty(); }
        bool HasDrawInfo() const { return HasOpaqueDrawInfo() || HasAlphaDrawInfo(); }

        uint64_t GetGeometryVersion() const { return m_geometryVersion; }

        Buffer *GetUniforms(uint32_t frame) { return m_storages[frame]; }
        Buffer *GetIndirect(uint32_t frame) { return m_indirects[frame]; }
        Buffer *GetIndirectAll() { return m_indirectAll; }
        Buffer *GetBuffer() { return m_buffer; }
        AccelerationStructure *GetTLAS() { return m_tlas; }
        Buffer *GetInstanceBuffer() { return m_instanceBuffer; }
        Buffer *GetMeshInfoBuffer() { return m_meshInfoBuffer; }
        size_t GetVerticesOffset() const { return m_verticesOffset; }
        size_t GetPositionsOffset() const { return m_positionsOffset; }
        size_t GetAabbVerticesOffset() const { return m_aabbVerticesOffset; }
        size_t GetAabbIndicesOffset() const { return m_aabbIndicesOffset; }
        const std::vector<DrawInfo> &GetDrawInfosOpaque() const { return m_drawInfosOpaque; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaCut() const { return m_drawInfosAlphaCut; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaBlend() const { return m_drawInfosAlphaBlend; }
        const std::vector<DrawInfo> &GetDrawInfosTransmission() const { return m_drawInfosTransmission; }
        const std::vector<ImageView *> &GetImageViews() const { return m_imageViews; }
        uint32_t GetMeshCount() const { return m_meshCount; }
        Buffer *GetMeshConstants() { return m_meshConstants; }
        Sampler *GetDefaultSampler() const;
        static const std::vector<uint32_t> &GetAabbIndices() { return s_aabbIndices; }

    private:
        void UpdateGeometry();
        void CullNode(Model &model, int node);
        void UpdateUniformData();
        void UpdateIndirectData();
        void ClearDrawInfos(bool reserveMax);
        void DestroyBuffers();
        void SortDrawInfos();
        void MarkUniformsDirty();
        void CreateGeometryBuffer();
        void CopyIndices(CommandBuffer *cmd);
        void CopyVertices(CommandBuffer *cmd);
        void CreateStorageBuffers();
        void CreateIndirectBuffers(CommandBuffer *cmd);
        void UpdateImageViews();
        void CreateMeshConstants(CommandBuffer *cmd);
        void BuildAccelerationStructures(CommandBuffer *cmd);

        struct alignas(64) PerFrameData
        {
            mat4 viewProjection;
            mat4 previousViewProjection;
            mat4 invView;
            mat4 invProjection;
        };

        PerFrameData m_frameData{};
        std::vector<Camera *> m_cameras;
        OrderedMap<size_t, Model *> m_models;

        ParticleManager *m_particleManager = nullptr;

        Buffer *m_buffer = nullptr;
        std::vector<Buffer *> m_storages;
        std::vector<Buffer *> m_indirects;
        Buffer *m_indirectAll = nullptr;

        size_t m_verticesOffset = 0;
        size_t m_positionsOffset = 0;
        size_t m_aabbVerticesOffset = 0;
        size_t m_aabbIndicesOffset = 0;
        uint32_t m_meshCount = 0;
        uint32_t m_indicesCount = 0;
        uint32_t m_verticesCount = 0;
        uint32_t m_positionsCount = 0;
        uint32_t m_aabbVerticesCount = 0;

        std::vector<ImageView *> m_imageViews;
        uint64_t m_geometryVersion = 0;

        std::vector<DrawInfo> m_drawInfosOpaque;
        std::vector<DrawInfo> m_drawInfosAlphaCut;
        std::vector<DrawInfo> m_drawInfosAlphaBlend;
        std::vector<DrawInfo> m_drawInfosTransmission;
        std::vector<vk::DrawIndexedIndirectCommand> m_indirectCommands;

        std::mutex m_drawInfosMutex;

        // Ray Tracing
        struct alignas(16) MeshInfoGPU
        {
            uint32_t indexOffset;
            uint32_t vertexOffset;
            int textures[5]; // BaseColor, Normal, Metallic, Occlusion, Emissive
            // int padding; // implicit padding or explicit? alignas(16) might need care.
            // 3 uints + 5 ints = 32 bytes (perfectly aligned to 16 if indices are 4 bytes)
            // 3*4 + 5*4 = 12 + 20 = 32 bytes. No padding needed if struct is 32 bytes.
        };
        std::vector<AccelerationStructure *> m_blases;
        AccelerationStructure *m_tlas = nullptr;
        Buffer *m_instanceBuffer = nullptr;
        Buffer *m_blasMergedBuffer = nullptr;
        Buffer *m_scratchBuffer = nullptr;
        Buffer *m_meshInfoBuffer = nullptr;
        Buffer *m_meshConstants = nullptr;
        Sampler *m_defaultSampler = nullptr;

        static std::vector<uint32_t> s_aabbIndices;
    };
} // namespace pe
