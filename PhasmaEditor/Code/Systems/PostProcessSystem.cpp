#include "Systems/PostProcessSystem.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/TonemapPass.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void PostProcessSystem::Init(CommandBuffer *cmd)
    {
        m_renderPassComponents[ID::GetTypeID<BloomBrightFilterPass>()] = CreateGlobalComponent<BloomBrightFilterPass>();
        m_renderPassComponents[ID::GetTypeID<BloomGaussianBlurHorizontalPass>()] = CreateGlobalComponent<BloomGaussianBlurHorizontalPass>();
        m_renderPassComponents[ID::GetTypeID<BloomGaussianBlurVerticalPass>()] = CreateGlobalComponent<BloomGaussianBlurVerticalPass>();
        m_renderPassComponents[ID::GetTypeID<DOFPass>()] = CreateGlobalComponent<DOFPass>();
        m_renderPassComponents[ID::GetTypeID<FXAAPass>()] = CreateGlobalComponent<FXAAPass>();
        m_renderPassComponents[ID::GetTypeID<MotionBlurPass>()] = CreateGlobalComponent<MotionBlurPass>();
        m_renderPassComponents[ID::GetTypeID<SSAOPass>()] = CreateGlobalComponent<SSAOPass>();
        m_renderPassComponents[ID::GetTypeID<SSRPass>()] = CreateGlobalComponent<SSRPass>();
        m_renderPassComponents[ID::GetTypeID<TonemapPass>()] = CreateGlobalComponent<TonemapPass>();

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(cmd);
        }

        // All passes (renderer + post-process) are now initialized; build the render graph
        GetGlobalSystem<RendererSystem>()->CacheGlobalComponents();
        GetGlobalSystem<RendererSystem>()->BuildRenderGraph();
    }

    void PostProcessSystem::Update()
    {
        PE_PROFILE_SCOPE("Post Process System");
        const auto &gSettings = Settings::Get<GlobalSettings>();

        auto *bbfp = GetEffect<BloomBrightFilterPass>();
        auto *bgbh = GetEffect<BloomGaussianBlurHorizontalPass>();
        auto *bgbv = GetEffect<BloomGaussianBlurVerticalPass>();
        auto *dof = GetEffect<DOFPass>();
        auto *fxaa = GetEffect<FXAAPass>();
        auto *motionBlur = GetEffect<MotionBlurPass>();
        auto *ssao = GetEffect<SSAOPass>();
        auto *ssr = GetEffect<SSRPass>();
        auto *tonemap = GetEffect<TonemapPass>();

        auto shouldUpdate = [&](IRenderPassComponent *rc)
        {
            if (!rc || !rc->IsEnabled())
                return false;

            if (rc == bbfp || rc == bgbh || rc == bgbv)
                return gSettings.bloom;
            if (rc == dof)
                return gSettings.dof;
            if (rc == fxaa)
                return gSettings.fxaa;
            if (rc == motionBlur)
                return gSettings.motion_blur;
            if (rc == ssao)
                return gSettings.ssao && gSettings.render_mode != RenderMode::RayTracing;
            if (rc == ssr)
                return gSettings.ssr && gSettings.render_mode != RenderMode::RayTracing;
            if (rc == tonemap)
                return gSettings.tonemapping;

            return true;
        };

        std::vector<std::shared_future<void>> futures;
        futures.reserve(m_renderPassComponents.size());
        for (auto &renderPassComponent : m_renderPassComponents)
        {
            if (!shouldUpdate(renderPassComponent))
                continue;

            futures.push_back(ThreadPool::Update.Enqueue([renderPassComponent]()
                                                         { renderPassComponent->Update(); }));
        }

        for (auto &future : futures)
            future.wait();
    }

    void PostProcessSystem::Destroy()
    {
        for (auto &renderPassComponent : m_renderPassComponents)
            renderPassComponent->Destroy();
    }

    void PostProcessSystem::Resize(uint32_t width, uint32_t height)
    {
        for (auto &renderPassComponent : m_renderPassComponents)
            renderPassComponent->Resize(width, height);
    }

    void PostProcessSystem::PollShaders(std::optional<size_t> hash)
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
        {
            std::shared_ptr<PassInfo> info = rc->GetPassInfo();

            bool match = false;
            if (!hash.has_value())
            {
                match = true;
            }
            else
            {
                if (info->pCompShader && info->pCompShader->GetPathID() == hash.value())
                    match = true;
                if (info->pVertShader && info->pVertShader->GetPathID() == hash.value())
                    match = true;
                if (info->pFragShader && info->pFragShader->GetPathID() == hash.value())
                    match = true;
            }

            if (match)
            {
                auto oldComp = info->pCompShader;
                auto oldVert = info->pVertShader;
                auto oldFrag = info->pFragShader;

                try
                {
                    rc->UpdatePassInfo();
                    rc->UpdateDescriptorSets();

                    Shader::Destroy(oldComp);
                    Shader::Destroy(oldVert);
                    Shader::Destroy(oldFrag);
                }
                catch (const std::exception &e)
                {
                    if (info->pCompShader != oldComp)
                        Shader::Destroy(info->pCompShader);
                    if (info->pVertShader != oldVert)
                        Shader::Destroy(info->pVertShader);
                    if (info->pFragShader != oldFrag)
                        Shader::Destroy(info->pFragShader);

                    info->pCompShader = oldComp;
                    info->pVertShader = oldVert;
                    info->pFragShader = oldFrag;
                }
            }
        }
    }
} // namespace pe
