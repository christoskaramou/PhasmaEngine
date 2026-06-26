#pragma once

#include <memory>
#include <vector>

namespace pe
{
    class Image;
    class CommandBuffer;
    class PassInfo;
    class Scene;

    // Screen-space selection outline (x-ray). Two steps in one pass:
    //   1) draw the frustum-culled selected indirect bucket (CullingCS slot 4) into an R8 mask,
    //   2) fullscreen composite samples the mask, edge-detects with smoothstep inner/outer fade,
    //      and alpha-blends the outline color over "display". Runs after Grid, before Particle.
    class SelectionOutlinePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override { UpdateDescriptorSets(); }
        void UpdateDescriptorSets() override;
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            std::vector<PassInfo *> passInfos{m_passInfo.get()};
            if (m_passInfoMask)
                passInfos.push_back(m_passInfoMask.get());
            return passInfos;
        }

        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        std::shared_ptr<PassInfo> m_passInfoMask;

        Image *m_colorRT = nullptr;
        Image *m_maskRT = nullptr;
        Scene *m_scene = nullptr;
    };
} // namespace pe
