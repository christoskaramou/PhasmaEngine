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

    // Two-phase temporal Hi-Z occlusion, late depth (priority 270, after CullPhase2@260 produced
    // "set B" = the newly-disoccluded opaque objects). Draws set B into the depth buffer with LOAD,
    // so the set-A depth written by DepthPass@200 is preserved and the buffer ends up holding the
    // nearest surface of A ∪ B. The G-buffer@300 then reverse-Z EQUAL-tests both sets against this
    // complete depth. Only enabled when SceneSettings::occlusion_culling is on; pipelines mirror
    // DepthPass (same DepthVS/DepthPS, depth write on).
    class DepthLatePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
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
        OpenPBRMask &GetMasks() { return m_masks; }
        void SyncMaskModules(Scene &scene, bool enabled)
        {
            // Mask pipelines write the visibility target: build them only while this pass has it.
            if (enabled && m_visibility)
                m_masks.Sync(scene);
            else
                m_masks.Clear();
        }

    private:
        OpenPBRMask m_masks;
        std::shared_ptr<PassInfo> m_passInfoDS;

        Image *m_depthStencil = nullptr;
        Image *m_visibility = nullptr;
        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = 0;
    };
} // namespace pe
