#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Pipeline.h"
#include "Shader/Shader.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "RenderPasses/TonemapPass.h"

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
        m_renderPassComponents[ID::GetTypeID<SuperResolutionPass>()] = CreateGlobalComponent<SuperResolutionPass>();
        m_renderPassComponents[ID::GetTypeID<TonemapPass>()] = CreateGlobalComponent<TonemapPass>();

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(cmd);
        }
    }

    void PostProcessSystem::Update()
    {
        Camera *camera_main = GetGlobalSystem<CameraSystem>()->GetCamera(0);

        std::vector<Task<void>> tasks;
        tasks.reserve(m_renderPassComponents.size());
        for (auto &renderPassComponent : m_renderPassComponents)
        {
            if (renderPassComponent->IsEnabled())
            {
                auto task = e_Update_ThreadPool.Enqueue([renderPassComponent, camera_main]()
                                                        { renderPassComponent->Update(camera_main); });
                tasks.push_back(std::move(task));
            }
        }

        for (auto &task : tasks)
            task.get();
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

    void PostProcessSystem::PollShaders()
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
        {
            auto info = rc->m_passInfo;

            // PollEvent simply catches a pushed event from FileWatcher
            if ((info->pCompShader && EventSystem::PollEvent(info->pCompShader->GetPathID())) ||
                (info->pVertShader && EventSystem::PollEvent(info->pVertShader->GetPathID())) ||
                (info->pFragShader && EventSystem::PollEvent(info->pFragShader->GetPathID())))
            {
                Shader::Destroy(info->pCompShader);
                Shader::Destroy(info->pVertShader);
                Shader::Destroy(info->pFragShader);

                info->pCompShader = nullptr;
                info->pVertShader = nullptr;
                info->pFragShader = nullptr;

                rc->UpdatePassInfo();
            }
        }
    }
}
