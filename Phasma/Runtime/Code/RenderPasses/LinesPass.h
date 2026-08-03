#pragma once

#include "API/Pipeline.h"

namespace pe
{
    class Image;
    class CommandBuffer;
    class Scene;

    // Draws hardware line strips and feathered sprite silhouette triangles directly
    // into the lit viewport after transparent lighting. Both bypass the indirect
    // G-buffer path; sprite outlines therefore blend exactly once.
    class LinesPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SetScene(Scene *scene) { m_scene = scene; }
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            return {m_passInfo.get(), m_spriteOutlinePassInfo.get()};
        }

    private:
        Image *m_viewportRT;
        Image *m_depthRT;
        Scene *m_scene = nullptr;
        std::unique_ptr<PassInfo> m_spriteOutlinePassInfo = std::make_unique<PassInfo>();
    };
} // namespace pe
