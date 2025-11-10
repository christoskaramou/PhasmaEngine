#include "Scene/Scene.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Command.h"
#include "API/Pipeline.h"
#include "RenderPasses/ShadowPass.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/LightPass.h"

namespace pe
{
    namespace
    {
        Sampler *defaultSampler = nullptr;
    }

    Scene::Scene()
    {
        if (!defaultSampler)
            defaultSampler = Sampler::Create(Sampler::CreateInfoInit(), "defaultSampler");
    }

    Scene::~Scene()
    {
        if (defaultSampler)
            Sampler::Destroy(defaultSampler);
    }

    void Scene::Update()
    {
        // Cull and update geometry
        m_geometry.UpdateGeometry();

        if (m_geometry.HasDrawInfo())
        {
            uint32_t frame = RHII.GetFrameIndex();

            // AabbsPass
            if (Settings::Get<GlobalSettings>().draw_aabbs)
            {
                AabbsPass *ap = GetGlobalComponent<AabbsPass>();
                const auto &sets = ap->m_passInfo->GetDescriptors(frame);

                // Set 0
                Descriptor *DSetUniforms = sets[0];
                DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                DSetUniforms->Update();
            }

            if (m_geometry.HasOpaqueDrawInfo())
            {
                // DepthPass
                {
                    GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
                    DepthPass *dp = GetGlobalComponent<DepthPass>();
                    const auto &sets = dp->m_passInfo->GetDescriptors(frame);

                    // set 0
                    Descriptor *DSetUniforms = sets[0];
                    DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                    DSetUniforms->SetBuffer(1, gb->m_constants);
                    DSetUniforms->Update();

                    // set 1
                    Descriptor *DSetTextures = sets[1];
                    DSetTextures->SetBuffer(0, gb->m_constants);
                    if (m_geometry.HasDirtyDescriptorViews(frame))
                    {
                        DSetTextures->SetSampler(1, defaultSampler->ApiHandle());
                        DSetTextures->SetImageViews(2, m_geometry.GetImageViews(), {});
                        DSetTextures->Update();
                    }
                }

                // ShadowPass Opaque
                {
                    GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
                    ShadowPass *shadows = GetGlobalComponent<ShadowPass>();
                    const auto &sets = shadows->m_passInfo->GetDescriptors(frame);

                    // set 0
                    Descriptor *DSetUniforms = sets[0];
                    DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                    DSetUniforms->SetBuffer(1, gb->m_constants);
                    DSetUniforms->Update();
                }

                // GbufferPass Opaque
                {
                    GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
                    const auto &sets = gb->m_passInfo->GetDescriptors(frame);

                    // set 0
                    Descriptor *DSetUniforms = sets[0];
                    DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                    DSetUniforms->SetBuffer(1, gb->m_constants);
                    DSetUniforms->Update();

                    // set 1
                    Descriptor *DSetTextures = sets[1];
                    DSetTextures->SetBuffer(0, gb->m_constants);
                    if (m_geometry.HasDirtyDescriptorViews(frame))
                    {
                        DSetTextures->SetSampler(1, defaultSampler->ApiHandle());
                        DSetTextures->SetImageViews(2, m_geometry.GetImageViews(), {});
                        DSetTextures->Update();
                    }
                }
            }
            // GbufferPass Trasparent
            if (m_geometry.HasAlphaDrawInfo())
            {
                GbufferTransparentPass *gb = GetGlobalComponent<GbufferTransparentPass>();
                const auto &sets = gb->m_passInfo->GetDescriptors(frame);

                // set 0
                Descriptor *DSetUniforms = sets[0];
                DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                DSetUniforms->SetBuffer(1, gb->m_constants);
                DSetUniforms->Update();

                // set 1
                Descriptor *DSetTextures = sets[1];
                DSetTextures->SetBuffer(0, gb->m_constants);
                if (m_geometry.HasDirtyDescriptorViews(frame))
                {
                    DSetTextures->SetSampler(1, defaultSampler->ApiHandle());
                    DSetTextures->SetImageViews(2, m_geometry.GetImageViews(), {});
                    DSetTextures->Update();
                }
            }

            m_geometry.ClearDirtyDescriptorViews(frame);
        }
    }

    void Scene::UpdateGeometryBuffers(CommandBuffer *cmd)
    {
        m_geometry.UploadBuffers(cmd);
    }

    void Scene::DrawShadowPass(CommandBuffer *cmd)
    {
        ShadowPass *shadows = GetGlobalComponent<ShadowPass>();
        shadows->SetGeometry(&m_geometry);
        shadows->Draw(cmd);
    }

    void Scene::DepthPrePass(CommandBuffer *cmd)
    {
        DepthPass *dp = GetGlobalComponent<DepthPass>();
        dp->SetGeometry(&m_geometry);
        dp->Draw(cmd);
    }

    void Scene::DrawGbufferPassOpaque(CommandBuffer *cmd)
    {
        GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
        gb->SetGeometry(&m_geometry);
        gb->Draw(cmd);
    }

    void Scene::DrawGbufferPassTransparent(CommandBuffer *cmd)
    {
        GbufferTransparentPass *gb = GetGlobalComponent<GbufferTransparentPass>();
        gb->SetGeometry(&m_geometry);
        gb->Draw(cmd);
    }

    void Scene::DrawLightPassOpaque(CommandBuffer *cmd)
    {
        LightOpaquePass *lp = GetGlobalComponent<LightOpaquePass>();
        lp->Draw(cmd);
    }

    void Scene::DrawLightPassTransparent(CommandBuffer *cmd)
    {
        LightTransparentPass *lp = GetGlobalComponent<LightTransparentPass>();
        lp->Draw(cmd);
    }

    void Scene::DrawAabbsPass(CommandBuffer *cmd)
    {
        AabbsPass *ap = GetGlobalComponent<AabbsPass>();
        ap->SetGeometry(&m_geometry);
        ap->Draw(cmd);
    }

    void Scene::DrawScene(CommandBuffer *cmd)
    {
    }

    void Scene::AddModel(ModelGltf *model)
    {
        m_geometry.AddModel(model);
    }

    void Scene::RemoveModel(ModelGltf *model)
    {
        m_geometry.RemoveModel(model);
    }
}
