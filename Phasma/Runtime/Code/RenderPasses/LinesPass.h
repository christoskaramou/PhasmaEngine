#pragma once

#include "API/Pipeline.h"

namespace pe
{
    class Image;
    class CommandBuffer;
    class Scene;

    // Draws hardware line strips, feathered sprite silhouette triangles, and unlit additive
    // effect meshes directly into the lit viewport after transparent lighting. All bypass
    // the indirect G-buffer path so additive meshes never write depth or clip each other.
    class LinesPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override;
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SetScene(Scene *scene) { m_scene = scene; }
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            return {m_passInfo.get(), m_spriteOutlinePassInfo.get(), m_additivePassInfo.get()};
        }

    private:
        Image *m_viewportRT;
        Image *m_depthRT;
        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = ~0ull;
        std::unique_ptr<PassInfo> m_spriteOutlinePassInfo = std::make_unique<PassInfo>();
        std::unique_ptr<PassInfo> m_additivePassInfo = std::make_unique<PassInfo>();
    };
} // namespace pe
