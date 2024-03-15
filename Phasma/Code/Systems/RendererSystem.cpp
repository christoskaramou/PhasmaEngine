#include "Systems/RendererSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPasses/DepthPass.h"
#include "Renderer/RenderPasses/GbufferPass.h"
#include "Renderer/RenderPasses/LightPass.h"
#include "Renderer/RenderPasses/AabbsPass.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
    std::string PresentModeToString(PresentMode presentMode)
    {
        switch (presentMode)
        {
        case PresentMode::Immediate:
            return "Immediate";
        case PresentMode::Mailbox:
            return "Mailbox";
        case PresentMode::Fifo:
            return "Fifo";
        case PresentMode::FifoRelaxed:
            return "FifoRelaxed";
        case PresentMode::SharedDemandRefresh:
            return "SharedDemandRefresh";
        case PresentMode::SharedContinuousRefresh:
            return "SharedContinuousRefresh";
        }

        PE_ERROR("Unknown PresentMode");
        return "";
    }

    void RendererSystem::Init(CommandBuffer *cmd)
    {
        Surface *surface = RHII.GetSurface();
        Format surfaceFormat = surface->GetFormat();

        // Set Window Title
        std::string title = "PhasmaEngine";
        title += " - Device: " + RHII.GetGpuName();
        title += " - API: Vulkan";
        title += " - Present Mode: " + PresentModeToString(surface->GetPresentMode());
#if _DEBUG
        title += " - Configuration: Debug";
#else
        title += " - Configuration: Release";
#endif // _DEBUG

        EventSystem::DispatchEvent(EventSetWindowTitle, title);

        // Create all render targets
        CreateRenderTargets();

        // Load Skyboxes
        LoadResources(cmd);

        // Create render components
        m_renderPassComponents[ID::GetTypeID<ShadowPass>()] = WORLD_ENTITY->CreateComponent<ShadowPass>();
        m_renderPassComponents[ID::GetTypeID<DepthPass>()] = WORLD_ENTITY->CreateComponent<DepthPass>();
        m_renderPassComponents[ID::GetTypeID<GbufferPass>()] = WORLD_ENTITY->CreateComponent<GbufferPass>();
        m_renderPassComponents[ID::GetTypeID<LightPass>()] = WORLD_ENTITY->CreateComponent<LightPass>();
        m_renderPassComponents[ID::GetTypeID<AabbsPass>()] = WORLD_ENTITY->CreateComponent<AabbsPass>();

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(cmd);
        }

        // Init GUI
        m_gui.InitGUI();

        m_renderArea.Update(0.0f, 0.0f, WIDTH_f, HEIGHT_f);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            ImageBarrierInfo barrierInfo{};
            barrierInfo.image = RHII.GetSwapchain()->GetImage(i);
            barrierInfo.layout = ImageLayout::PresentSrc;
            barrierInfo.stageFlags = PipelineStage::AllCommandsBit;
            barrierInfo.accessMask = Access::None;
            cmd->ImageBarrier(barrierInfo); // transition from undefined to present

            m_cmds[i] = std::vector<CommandBuffer *>{};
        }
    }

    void RendererSystem::Update(double delta)
    {
        // Wait for the previous corresponding frame commands to finish first
        WaitPreviousFrameCommands();

#if PE_SCRIPTS
        // universal scripts
        for (auto &s : scripts)
            s->update(static_cast<float>(delta));
#endif
        // GUI
        m_gui.Update();

        // Scene
        m_scene.Update(delta);

        CameraSystem *cameraSystem = CONTEXT->GetSystem<CameraSystem>();
        Camera *camera_main = cameraSystem->GetCamera(0);

        // Render Components
        std::vector<Task<void>> tasks;
        tasks.reserve(m_renderPassComponents.size());
        for (auto &rc : m_renderPassComponents)
        {
            auto task = e_Update_ThreadPool.Enqueue([rc, camera_main]()
                                             { rc->Update(camera_main); });
            tasks.push_back(std::move(task));
        }
        for (auto &task : tasks)
            task.get();
    }

    void RendererSystem::WaitPreviousFrameCommands()
    {
        uint32_t frameIndex = RHII.GetFrameIndex();
        auto &frameCmds = m_cmds[frameIndex];

        // Wait for unfinished work
        for (auto &cmd : frameCmds)
        {
            if (cmd)
            {
                cmd->Wait();
                CommandBuffer::Return(cmd);
            }
        }
        frameCmds.clear();
    }

    void RendererSystem::Draw()
    {
        uint32_t frameIndex = RHII.GetFrameIndex();

        // acquire the image
        Semaphore *aquireSignalSemaphore = RHII.GetSemaphores(frameIndex)[0];
        Swapchain *swapchain = RHII.GetSwapchain();
        uint32_t imageIndex = swapchain->Aquire(aquireSignalSemaphore);

        static Timer timer;
        timer.Start();

        auto &frameCmds = m_cmds[frameIndex];

        // RECORD COMMANDS
        frameCmds = RecordPasses(imageIndex);

        // SUBMIT TO QUEUE
        aquireSignalSemaphore->SetWaitStageFlags(PipelineStage::ColorAttachmentOutputBit | PipelineStage::ComputeShaderBit);
        Semaphore *submitsSemaphore = Queue::SubmitCommands(aquireSignalSemaphore, frameCmds);

        // PRESENT
        RHII.GetRenderQueue()->Present(1, &swapchain, &imageIndex, 1, &submitsSemaphore);
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
            rc->Destroy();

#if PE_SCRIPTS
        for (auto &script : scripts)
            delete script;
#endif

        m_skyBoxDay.Destroy();
        m_skyBoxNight.Destroy();
        m_gui.Destroy();

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);

        for (auto &dt : m_depthStencilTargets)
            Image::Destroy(dt.second);
    }
}
