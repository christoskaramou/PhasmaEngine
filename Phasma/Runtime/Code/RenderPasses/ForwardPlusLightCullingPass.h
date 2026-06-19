#pragma once

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class Image;

    struct ForwardPlusLightCullingUBO
    {
        mat4 viewProj = mat4(1.0f);
        vec4 cameraRight = vec4(1.0f, 0.0f, 0.0f, 0.0f);
        vec4 cameraUp = vec4(0.0f, 1.0f, 0.0f, 0.0f);
        vec4 framebufferAndTiles = vec4(1.0f, 1.0f, 1.0f, 1.0f);
    };

    class ForwardPlusLightCullingPass : public IRenderPassComponent
    {
    public:
        static constexpr uint32_t TileSize = 16;
        static constexpr uint32_t MaxLightsPerTile = 128;

        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        Buffer *GetTileLightData(uint32_t frame) const;
        Buffer *GetPointLightIndices(uint32_t frame) const;
        Buffer *GetSpotLightIndices(uint32_t frame) const;
        uint32_t GetTileCountX() const { return m_tileCountX; }
        uint32_t GetTileCountY() const { return m_tileCountY; }
        bool HasLiveResources() const;

    private:
        void EnsureBuffers();
        void DestroyBuffers();
        size_t TileCount() const;

        Image *m_viewportRT = nullptr;
        std::vector<Buffer *> m_uniforms;
        std::vector<Buffer *> m_tileLightData;
        std::vector<Buffer *> m_pointLightIndices;
        std::vector<Buffer *> m_spotLightIndices;
        std::vector<Buffer *> m_boundLightUniforms;
        std::vector<Buffer *> m_boundLightStorage;
        ForwardPlusLightCullingUBO m_ubo;
        uint32_t m_tileCountX = 1;
        uint32_t m_tileCountY = 1;
        uint32_t m_bufferWidth = 0;
        uint32_t m_bufferHeight = 0;
    };
} // namespace pe
