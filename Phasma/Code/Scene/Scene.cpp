#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Shader/Shader.h"
#include "Renderer/Image.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/Pipeline.h"
#include "Renderer/RenderPasses/ShadowPass.h"
#include "Renderer/RenderPasses/AabbsPass.h"
#include "Renderer/RenderPasses/GbufferPass.h"
#include "Renderer/RenderPasses/DepthPass.h"
#include "Renderer/RenderPasses/LightPass.h"

namespace pe
{
    namespace
    {
        Sampler *defaultSampler = nullptr;
    }

    Scene::Scene()
    {
        if (!defaultSampler)
            defaultSampler = Sampler::Create(SamplerCreateInfo{});
    }

    Scene::~Scene()
    {
        if (defaultSampler)
            Sampler::Destroy(defaultSampler);
    }

    void Scene::Update(double delta)
    {
        // Cull and update geometry shader resources
        m_geometry.UpdateGeometry();

        if (m_geometry.HasDrawInfo())
        {
            uint32_t frame = RHII.GetFrameIndex();

            std::vector<Task<void>> tasks{};
            tasks.reserve(10);

            // AabbsPass
            if (Settings::Get<GlobalSettings>().draw_aabbs)
            {
                auto task = e_Update_ThreadPool.Enqueue(
                    [this, frame]()
                    {
                        AabbsPass *ap = GetGlobalComponent<AabbsPass>();
                        const auto &sets = ap->m_passInfo->GetDescriptors(frame);

                        // Set 0
                        Descriptor *DSetUniforms = sets[0];
                        DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                        DSetUniforms->Update();
                    });

                tasks.push_back(std::move(task));
            }

            if (m_geometry.HasOpaqueDrawInfo())
            {
                // DepthPass
                {
                    auto task = e_Update_ThreadPool.Enqueue(
                        [this, frame]()
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
                        });

                    tasks.push_back(std::move(task));
                }

                // ShadowPass Opaque
                {
                    auto task = e_Update_ThreadPool.Enqueue(
                        [this, frame]()
                        {
                            GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
                            ShadowPass *shadows = GetGlobalComponent<ShadowPass>();
                            const auto &sets = shadows->m_passInfo->GetDescriptors(frame);

                            // set 0
                            Descriptor *DSetUniforms = sets[0];
                            DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                            DSetUniforms->SetBuffer(1, gb->m_constants);
                            DSetUniforms->Update();
                        });

                    tasks.push_back(std::move(task));
                }

                // GbufferPass Opaque
                {
                    auto task = e_Update_ThreadPool.Enqueue(
                        [this, frame]()
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
                        });

                    tasks.push_back(std::move(task));
                }
            }
            // GbufferPass Trasparent
            if (m_geometry.HasAlphaDrawInfo())
            {
                auto task = e_Update_ThreadPool.Enqueue(
                    [this, frame]()
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
                    });

                tasks.push_back(std::move(task));
            }

            for (auto &task : tasks)
                task.get();

            m_geometry.ClearDirtyDescriptorViews(frame);
        }
    }

    void Scene::InitGeometry(CommandBuffer *cmd)
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
