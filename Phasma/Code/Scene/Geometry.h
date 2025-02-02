#pragma once

namespace pe
{
    class ModelGltf;
    class Buffer;
    class CommandBuffer;
    class Camera;
    class Image;
    class Sampler;

    struct DrawInfo
    {
        ModelGltf *model;
        int node;
        int primitive;
        float distance;
    };

    struct DrawIndexedIndirectCommand
    {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t vertexOffset;
        uint32_t firstInstance;
    };

    class Geometry
    {
    public:
        Geometry();
        ~Geometry();

        void AddModel(ModelGltf *modell);
        void RemoveModel(ModelGltf *model);
        void UploadBuffers(CommandBuffer *cmd);
        void UpdateGeometry();
        bool HasOpaqueDrawInfo() { return !m_drawInfosOpaque.empty(); }
        bool HasAlphaDrawInfo() { return !m_drawInfosAlphaCut.empty() || !m_drawInfosAlphaBlend.empty(); }
        bool HasDrawInfo() { return HasOpaqueDrawInfo() || HasAlphaDrawInfo(); }
        bool HasDirtyDescriptorViews(uint32_t frame) { return m_dirtyDescriptorViews[frame]; }
        void ClearDirtyDescriptorViews(uint32_t frame) { m_dirtyDescriptorViews[frame] = false; }
        // Getters
        std::vector<ImageViewApiHandle> &GetImageViews() { return m_imageViews; }
        OrderedMap<size_t, ModelGltf *> &GetModels() { return m_models; }
        Buffer *GetUniforms(uint32_t frame) { return m_storage[frame]; }
        Buffer *GetIndirect(uint32_t frame) { return m_indirect[frame]; }
        Buffer *GetIndirectAll() { return m_indirectAll; }
        Buffer *GetBuffer() { return m_buffer; }
        size_t GetVerticesOffset() { return m_verticesOffset; }
        size_t GetPositionsOffset() { return m_positionsOffset; }
        size_t GetAabbVerticesOffset() { return m_aabbVerticesOffset; }
        size_t GetAabbIndicesOffset() { return m_aabbIndicesOffset; }
        std::vector<uint32_t> &GetAabbIndices() { return m_aabbIndices; }
        const std::vector<DrawInfo> &GetDrawInfosOpaque() const { return m_drawInfosOpaque; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaCut() const { return m_drawInfosAlphaCut; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaBlend() const { return m_drawInfosAlphaBlend; }
        uint32_t GetPrimitivesCount() { return m_primitivesCount; }

    private:
        void CullNodePrimitives(ModelGltf &model, int node);
        void UpdateUniformData();
        void UpdateIndirectData();
        void ClearDrawInfos(bool reserveMax);
        void DestroyBuffers();

        struct alignas(64) PerFrameData
        {
            mat4 viewProjection;
            mat4 previousViewProjection;
        };

        PerFrameData m_frameData;
        std::vector<Camera *> m_cameras{};
        OrderedMap<size_t, ModelGltf *> m_models{};
        Buffer *m_buffer;                     // geometry buffer
        Buffer *m_storage[SWAPCHAIN_IMAGES];  // per frame storage buffer
        Buffer *m_indirect[SWAPCHAIN_IMAGES]; // pre frame indirect commands buffer
        Buffer *m_indirectAll;                // pre frame indirect commands buffer
        size_t m_verticesOffset = 0;          // in bytes (offset in geometryBuffer), for Vertex range
        size_t m_positionsOffset = 0;         // in bytes (offset in geometryBuffer), for PositionUvVertex range
        size_t m_aabbVerticesOffset = 0;      // in bytes (offset in geometryBuffer), for AABB range
        size_t m_aabbIndicesOffset = 0;       // in bytes (offset in geometryBuffer), for Shadows range
        std::vector<uint32_t> m_aabbIndices;
        std::vector<ImageViewApiHandle> m_imageViews{};
        std::vector<Sampler *> m_samplers{};
        bool m_dirtyDescriptorViews[SWAPCHAIN_IMAGES];
        std::vector<DrawInfo> m_drawInfosOpaque;
        std::vector<DrawInfo> m_drawInfosAlphaCut;
        std::vector<DrawInfo> m_drawInfosAlphaBlend;
        uint32_t m_primitivesCount = 0;
        std::vector<DrawIndexedIndirectCommand> m_indirectCommands{};
    };
}
