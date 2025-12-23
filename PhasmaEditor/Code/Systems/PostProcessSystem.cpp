#include "Systems/PostProcessSystem.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "RenderPasses/TonemapPass.h"
#include "Systems/CameraSystem.h"

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

        std::vector<std::shared_future<void>> futures;
        futures.reserve(m_renderPassComponents.size());
        for (auto &renderPassComponent : m_renderPassComponents)
        {
            if (renderPassComponent->IsEnabled())
            {
                futures.push_back(ThreadPool::Update.Enqueue([renderPassComponent, camera_main]()
                                                             { renderPassComponent->Update(); }));
            }
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

    void PostProcessSystem::PollShaders()
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
        {
            std::shared_ptr<PassInfo> info = rc->GetPassInfo();

            // PollEvent simply catches a pushed event from FileWatcher
            EventSystem::QueuedEvent event;
            if ((info->pCompShader && EventSystem::PeekEvent(static_cast<size_t>(info->pCompShader->GetPathID()), event)) ||
                (info->pVertShader && EventSystem::PeekEvent(static_cast<size_t>(info->pVertShader->GetPathID()), event)) ||
                (info->pFragShader && EventSystem::PeekEvent(static_cast<size_t>(info->pFragShader->GetPathID()), event)))
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
} // namespace pe
