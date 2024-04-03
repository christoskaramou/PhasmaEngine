#include "Scene/Scene.h"
#include "Renderer/RenderPasses/GbufferPass.h"
#include "Renderer/RenderPasses/DepthPass.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPasses/LightPass.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPasses/ShadowPass.h"
#include "Renderer/RenderPasses/AabbsPass.h"

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

        m_renderQueue = RHII.GetRenderQueue();
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
                        const auto &sets = ap->m_passInfo.GetDescriptors(frame);

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
                            DepthPass *dp = GetGlobalComponent<DepthPass>();
                            const auto &sets = dp->m_passInfo.GetDescriptors(frame);

                            // set 0
                            Descriptor *DSetUniforms = sets[0];
                            DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                            DSetUniforms->Update();

                            // set 1
                            if (m_geometry.HasDirtyDescriptorViews(frame))
                            {
                                Descriptor *DSetTextures = sets[1];
                                DSetTextures->SetSampler(0, defaultSampler->ApiHandle());
                                DSetTextures->SetImageViews(1, m_geometry.GetImageViews(), {});
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
                            ShadowPass *shadows = GetGlobalComponent<ShadowPass>();
                            const auto &sets = shadows->m_passInfo.GetDescriptors(frame);

                            // set 0
                            Descriptor *DSetUniforms = sets[0];
                            DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                            DSetUniforms->Update();
                        });

                    tasks.push_back(std::move(task));
                }

                // GbufferPass Opaque
                {
                    auto task = e_Update_ThreadPool.Enqueue(
                        [this, frame]()
                        {
                            GbufferPass *gb = GetGlobalComponent<GbufferPass>();
                            const auto &sets = gb->m_passInfoOpaque.GetDescriptors(frame);

                            // set 0
                            Descriptor *DSetUniforms = sets[0];
                            DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                            DSetUniforms->Update();

                            // set 1
                            if (m_geometry.HasDirtyDescriptorViews(frame))
                            {
                                Descriptor *DSetTextures = sets[1];
                                DSetTextures->SetSampler(0, defaultSampler->ApiHandle());
                                DSetTextures->SetImageViews(1, m_geometry.GetImageViews(), {});
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
                        GbufferPass *gb = GetGlobalComponent<GbufferPass>();
                        const auto &sets = gb->m_passInfoAlpha.GetDescriptors(frame);

                        // set 0
                        Descriptor *DSetUniforms = sets[0];
                        DSetUniforms->SetBuffer(0, m_geometry.GetUniforms(frame));
                        DSetUniforms->Update();

                        // set 1
                        if (m_geometry.HasDirtyDescriptorViews(frame))
                        {
                            Descriptor *DSetTextures = sets[1];
                            DSetTextures->SetSampler(0, defaultSampler->ApiHandle());
                            DSetTextures->SetImageViews(1, m_geometry.GetImageViews(), {});
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

    CommandBuffer *Scene::DrawShadowPass()
    {
        ShadowPass *shadows = GetGlobalComponent<ShadowPass>();
        shadows->SetGeometry(&m_geometry);

        return shadows->Draw();
    }

    CommandBuffer *Scene::DepthPrePass()
    {
        DepthPass *dp = GetGlobalComponent<DepthPass>();
        dp->SetGeometry(&m_geometry);

        return dp->Draw();
    }

    CommandBuffer *Scene::DrawGbufferPassOpaque()
    {
        GbufferPass *gb = GetGlobalComponent<GbufferPass>();
        gb->SetGeometry(&m_geometry);
        gb->SetBlendType(BlendType::Opaque);

        return gb->Draw();
    }

    CommandBuffer *Scene::DrawGbufferPassTransparent()
    {
        GbufferPass *gb = GetGlobalComponent<GbufferPass>();
        gb->SetGeometry(&m_geometry);
        gb->SetBlendType(BlendType::Transparent);

        return gb->Draw();
    }

    CommandBuffer *Scene::DrawLightPassOpaque()
    {
        LightPass *lp = GetGlobalComponent<LightPass>();
        lp->SetBlendType(BlendType::Opaque);

        return lp->Draw();
    }

    CommandBuffer *Scene::DrawLightPassTransparent()
    {
        LightPass *lp = GetGlobalComponent<LightPass>();
        lp->SetBlendType(BlendType::Transparent);

        return lp->Draw();
    }

    CommandBuffer *Scene::DrawAabbsPass()
    {
        AabbsPass *ap = GetGlobalComponent<AabbsPass>();
        ap->SetGeometry(&m_geometry);

        return ap->Draw();
    }

    void Scene::DrawScene()
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
