#pragma once

#include "API/Pipeline.h"

namespace pe
{
    class Scene;
    class CommandBuffer;

    class CullingPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override {}
        void Update() override {}
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override {}
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override { return {m_passInfo.get(), m_sortPassInfo.get()}; }

        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        Scene *m_scene = nullptr;
        std::unique_ptr<PassInfo> m_sortPassInfo = std::make_unique<PassInfo>();
    };
} // namespace pe
