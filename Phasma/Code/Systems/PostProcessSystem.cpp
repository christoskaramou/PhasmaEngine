#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Renderer/RenderPasses/BloomPass.h"
#include "Renderer/RenderPasses/DOFPass.h"
#include "Renderer/RenderPasses/FXAAPass.h"
#include "Renderer/RenderPasses/MotionBlurPass.h"
#include "Renderer/RenderPasses/SSAOPass.h"
#include "Renderer/RenderPasses/SSRPass.h"
#include "Renderer/RenderPasses/SuperResolutionPass.h"
#include "Renderer/RenderPasses/TonemapPass.h"

namespace pe
{
    PostProcessSystem::PostProcessSystem()
    {
    }

    PostProcessSystem::~PostProcessSystem()
    {
    }

    void PostProcessSystem::Init(CommandBuffer *cmd)
    {
        m_renderPassComponents[ID::GetTypeID<BloomPass>()] = CreateGlobalComponent<BloomPass>();
        m_renderPassComponents[ID::GetTypeID<DOFPass>()] = CreateGlobalComponent<DOFPass>();
        m_renderPassComponents[ID::GetTypeID<FXAAPass>()] = CreateGlobalComponent<FXAAPass>();
        m_renderPassComponents[ID::GetTypeID<MotionBlurPass>()] = CreateGlobalComponent<MotionBlurPass>();
        m_renderPassComponents[ID::GetTypeID<SSAOPass>()] = CreateGlobalComponent<SSAOPass>();
        m_renderPassComponents[ID::GetTypeID<SSRPass>()] = CreateGlobalComponent<SSRPass>();
        m_renderPassComponents[ID::GetTypeID<SuperResolutionPass>()] = CreateGlobalComponent<SuperResolutionPass>();
        m_renderPassComponents[ID::GetTypeID<TonemapPass>()] = CreateGlobalComponent<TonemapPass>();

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent.second->Init();
            renderPassComponent.second->UpdatePassInfo();
            renderPassComponent.second->CreateUniforms(cmd);
        }
    }

    void PostProcessSystem::Update(double delta)
    {
        Camera *camera_main = GetGlobalSystem<CameraSystem>()->GetCamera(0);

        std::vector<Task<void>> tasks;
        tasks.reserve(m_renderPassComponents.size());
        for (auto &renderPassComponent : m_renderPassComponents)
        {
            if (renderPassComponent.second->IsEnabled())
            {
                auto task = e_Update_ThreadPool.Enqueue([renderPassComponent, camera_main]()
                                                        { renderPassComponent.second->Update(camera_main); });
                tasks.push_back(std::move(task));
            }
        }

        for (auto &task : tasks)
            task.get();
    }

    void PostProcessSystem::Destroy()
    {
        for (auto &renderPassComponent : m_renderPassComponents)
            renderPassComponent.second->Destroy();
    }

    void PostProcessSystem::Resize(uint32_t width, uint32_t height)
    {
        for (auto &renderPassComponent : m_renderPassComponents)
            renderPassComponent.second->Resize(width, height);
    }
}
