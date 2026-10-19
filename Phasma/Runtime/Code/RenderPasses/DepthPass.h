#pragma once

#include "OpenPBRMask.h"

#include <memory>
#include <vector>

namespace pe
{
    class Image;
    class CommandBuffer;
    class PassInfo;
    class Scene;
    struct BlendState;

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
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override
        {
            std::vector<PassInfo *> passInfos{m_passInfo.get()};
            if (m_passInfoDS)
                passInfos.push_back(m_passInfoDS.get());
            m_masks.AppendPassInfos(passInfos);
            return passInfos;
        }

        void SetScene(Scene *scene) { m_scene = scene; }
        void SyncMaskModules(Scene &scene, bool enabled)
        {
            // Mask pipelines write the visibility target: build them only while this pass has it.
            if (enabled && m_visibility)
                m_masks.Sync(scene);
            else
                m_masks.Clear();
        }
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        OpenPBRMask m_masks;
        std::shared_ptr<PassInfo> m_passInfoDS;

        Image *m_depthStencil;
        Image *m_visibility = nullptr;
        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = 0;
    };

    // Integer visibility attachments overwrite; write=false preserves the prepass payload in the G-buffer.
    BlendState VisibilityBlendState(bool write);
} // namespace pe
