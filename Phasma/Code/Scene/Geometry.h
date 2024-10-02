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

        bool operator<(const DrawInfo &rhs) const
        {
            return distance < rhs.distance;
        }
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
        Buffer *GetUniforms(uint32_t frame) { return m_uniforms[frame]; }
        Buffer *GetBuffer() { return m_buffer; }
        size_t GetVerticesOffset() { return m_verticesOffset; }
        size_t GetPositionsOffset() { return m_positionsOffset; }
        size_t GetAabbVerticesOffset() { return m_aabbVerticesOffset; }
        size_t GetAabbIndicesOffset() { return m_aabbIndicesOffset; }
        std::vector<uint32_t> &GetAabbIndices() { return m_aabbIndices; }
        std::multimap<float, DrawInfo> &GetDrawInfosOpaque() { return m_drawInfosOpaque; }
        std::multimap<float, DrawInfo, std::greater<float>> &GetDrawInfosAlphaCut() { return m_drawInfosAlphaCut; }
        std::multimap<float, DrawInfo, std::greater<float>> &GetDrawInfosAlphaBlend() { return m_drawInfosAlphaBlend; }

    private:
        void CullNodePrimitives(ModelGltf &model, int node);
        void UpdateUniformData();
        void ClearDrawInfos();
        void DestroyBuffers();

        struct CameraData
        {
            mat4 viewProjection;
            mat4 previousViewProjection;
        };

        CameraData m_cameraData;
        std::vector<Camera *> m_cameras{};
        OrderedMap<size_t, ModelGltf *> m_models{};
        Buffer *m_uniforms[SWAPCHAIN_IMAGES];   // per frame uniform buffer
        Buffer *m_buffer;                       // geometry buffer
        size_t m_verticesOffset = 0;            // in bytes (offset in geometryBuffer), for Vertex range
        size_t m_positionsOffset = 0;           // in bytes (offset in geometryBuffer), for PositionUvVertex range
        size_t m_aabbVerticesOffset = 0;        // in bytes (offset in geometryBuffer), for AABB range
        size_t m_aabbIndicesOffset = 0;         // in bytes (offset in geometryBuffer), for Shadows range
        std::vector<uint32_t> m_aabbIndices;
        std::vector<ImageViewApiHandle> m_imageViews{};
        std::vector<Sampler *> m_samplers{};
        bool m_dirtyDescriptorViews[SWAPCHAIN_IMAGES];
        std::mutex m_mutexDrawInfos;
        std::multimap<float, DrawInfo> m_drawInfosOpaque;
        std::multimap<float, DrawInfo, std::greater<float>> m_drawInfosAlphaCut;
        std::multimap<float, DrawInfo, std::greater<float>> m_drawInfosAlphaBlend;
        std::mutex m_mutex;
        std::unordered_map<ModelGltf *, std::unordered_map<int, std::vector<int>>> m_opaque;
        std::unordered_map<ModelGltf *, std::unordered_map<int, std::vector<int>>> m_alphaCut;
        std::unordered_map<ModelGltf *, std::unordered_map<int, std::vector<int>>> m_alphaBlend;
    };
}
