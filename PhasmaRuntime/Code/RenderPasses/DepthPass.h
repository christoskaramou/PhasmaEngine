#pragma once

#include <memory>
#include <vector>

namespace pe
{
    class Image;
    class CommandBuffer;
    class PassInfo;
    class Scene;

    struct PushConstants_DepthPass
    {
        uint32_t jointsCount;
    };

    class DepthPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            std::vector<PassInfo *> passInfos{m_passInfo.get()};
            if (m_passInfoDS)
                passInfos.push_back(m_passInfoDS.get());
            return passInfos;
        }

        void SetScene(Scene *scene) { m_scene = scene; }
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        std::shared_ptr<PassInfo> m_passInfoDS;

        Image *m_depthStencil;
        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = 0;
    };
} // namespace pe
