#pragma once

#include "Base/Base.h"

namespace pe
{
    class Buffer;
    class Camera;
    class CommandBuffer;
    class Image;
    class Model;

    struct DrawInfo
    {
        Model *model;
        int node;
        float distance;
    };

    class Scene
    {
    public:
        Scene();
        ~Scene();

        void Update();
        void UpdateGeometryBuffers();
        void AddModel(Model *model);
        void RemoveModel(Model *model);

        OrderedMap<size_t, Model *> &GetModels() { return m_models; }
        const OrderedMap<size_t, Model *> &GetModels() const { return m_models; }

        bool HasOpaqueDrawInfo() const { return !m_drawInfosOpaque.empty(); }
        bool HasAlphaDrawInfo() const { return !m_drawInfosAlphaCut.empty() || !m_drawInfosAlphaBlend.empty(); }
        bool HasDrawInfo() const { return HasOpaqueDrawInfo() || HasAlphaDrawInfo(); }
        bool HasDirtyDescriptorViews(uint32_t frame) const { return m_dirtyDescriptorViews[frame]; }
        void ClearDirtyDescriptorViews(uint32_t frame) { m_dirtyDescriptorViews[frame] = false; }

        Buffer *GetUniforms(uint32_t frame) { return m_storages[frame]; }
        Buffer *GetIndirect(uint32_t frame) { return m_indirects[frame]; }
        Buffer *GetIndirectAll() { return m_indirectAll; }
        Buffer *GetBuffer() { return m_buffer; }
        size_t GetVerticesOffset() const { return m_verticesOffset; }
        size_t GetPositionsOffset() const { return m_positionsOffset; }
        size_t GetAabbVerticesOffset() const { return m_aabbVerticesOffset; }
        size_t GetAabbIndicesOffset() const { return m_aabbIndicesOffset; }
        const std::vector<DrawInfo> &GetDrawInfosOpaque() const { return m_drawInfosOpaque; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaCut() const { return m_drawInfosAlphaCut; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaBlend() const { return m_drawInfosAlphaBlend; }
        const std::vector<vk::ImageView> &GetImageViews() const { return m_imageViews; }
        uint32_t GetMeshCount() const { return m_meshCount; }
        static const std::vector<uint32_t> &GetAabbIndices() { return s_aabbIndices; }

    private:
        void UpdateGeometry();
        void UploadBuffers(CommandBuffer *cmd);
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
        void CreateGBufferConstants(CommandBuffer *cmd);

        struct alignas(64) PerFrameData
        {
            mat4 viewProjection;
            mat4 previousViewProjection;
        };

        PerFrameData m_frameData{};
        std::vector<Camera *> m_cameras;
        OrderedMap<size_t, Model *> m_models;

        Buffer *m_buffer = nullptr;
        std::vector<Buffer *> m_storages;
        std::vector<Buffer *> m_indirects;
        Buffer *m_indirectAll = nullptr;

        size_t m_verticesOffset = 0;
        size_t m_positionsOffset = 0;
        size_t m_aabbVerticesOffset = 0;
        size_t m_aabbIndicesOffset = 0;
        uint32_t m_meshCount = 0;

        std::vector<vk::ImageView> m_imageViews;
        std::vector<bool> m_dirtyDescriptorViews;

        std::vector<DrawInfo> m_drawInfosOpaque;
        std::vector<DrawInfo> m_drawInfosAlphaCut;
        std::vector<DrawInfo> m_drawInfosAlphaBlend;
        std::vector<vk::DrawIndexedIndirectCommand> m_indirectCommands;

        std::mutex m_drawInfosMutex;

        static std::vector<uint32_t> s_aabbIndices;
    };
} // namespace pe
