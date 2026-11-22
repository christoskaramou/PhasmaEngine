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

    // A capsule a script keeps visible (phasma::World::SetCutawayVolume, render_graph.set_cutaway): the depth
    // prepass opens the meshes marked for it (Material::cutaway) where they stand between the camera and the capsule,
    // fully inside its outline and as an ordered dither across the feather (Common/Cutaway.hlsl). World units; a
    // radius of 0 at both ends turns it off. It lasts until set again and is cleared when its script system stops.
    struct Cutaway
    {
        vec3 a = vec3(0.0f);
        vec3 b = vec3(0.0f);
        float radiusA = 0.0f;
        float radiusB = 0.0f;
        float feather = 0.0f;
    };

    void SetCutaway(const Cutaway &cutaway);

    // Common/Cutaway.hlsl's PeCutaway for a target of that size, from the active camera; zero when off.
    struct CutawayConstants
    {
        vec4 a = vec4(0.0f);
        vec4 b = vec4(0.0f);
        vec4 params = vec4(0.0f);
    };

    CutawayConstants GetCutawayConstants(float width, float height);

    struct PushConstants_DepthPass
    {
        uint32_t jointsCount = 0;
        float padding[3] = {};
        CutawayConstants cutaway;
    };

    // What DepthPass and DepthLatePass push: the joint count and the cutaway for that depth target.
    PushConstants_DepthPass MakeDepthPassConstants(const Scene &scene, const Image &depth);

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
        OpenPBRMask &GetMasks() { return m_masks; }
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
