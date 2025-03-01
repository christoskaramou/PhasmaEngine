#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Image.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RenderPasses/DepthPass.h"
#include "Renderer/RenderPasses/GbufferPass.h"
#include "Renderer/RenderPasses/LightPass.h"
#include "Renderer/RenderPasses/AabbsPass.h"
#include "Renderer/RenderPasses/ShadowPass.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
    const char *PresentModeToString(PresentMode presentMode)
    {
        const char *modesNames[] = {"Immediate", "Mailbox", "Fifo", "FifoRelaxed", ""};
        switch (presentMode)
        {
        case PresentMode::Immediate:
            return modesNames[0];
        case PresentMode::Mailbox:
            return modesNames[1];
        case PresentMode::Fifo:
            return modesNames[2];
        case PresentMode::FifoRelaxed:
            return modesNames[3];
        }

        PE_ERROR("Unknown PresentMode");
        return modesNames[5];
    }

    void RendererSystem::Init(CommandBuffer *cmd)
    {
        Surface *surface = RHII.GetSurface();
        Format surfaceFormat = surface->GetFormat();

        // Set Window Title
        std::string title = "PhasmaEngine";
        title += " - Device: " + RHII.GetGpuName();
        title += " - API: Vulkan";
        title += " - Present Mode: " + std::string(PresentModeToString(surface->GetPresentMode()));
#if PE_DEBUG
        title += " - Debug";
#elif PE_RELEASE
        title += " - Release";
#elif PE_MINSIZEREL
        title += " - MinSizeRel";
#elif PE_RELWITHDEBINFO
        title += " - RelWithDebInfo";
#endif

        EventSystem::DispatchEvent(EventSetWindowTitle, title);

        // Create all render targets
        CreateRenderTargets();

        // Load Skyboxes
        LoadResources(cmd);

        // Create render components
        m_renderPassComponents[ID::GetTypeID<ShadowPass>()] = CreateGlobalComponent<ShadowPass>();
        m_renderPassComponents[ID::GetTypeID<DepthPass>()] = CreateGlobalComponent<DepthPass>();
        m_renderPassComponents[ID::GetTypeID<GbufferOpaquePass>()] = CreateGlobalComponent<GbufferOpaquePass>();
        m_renderPassComponents[ID::GetTypeID<GbufferTransparentPass>()] = CreateGlobalComponent<GbufferTransparentPass>();
        m_renderPassComponents[ID::GetTypeID<LightOpaquePass>()] = CreateGlobalComponent<LightOpaquePass>();
        m_renderPassComponents[ID::GetTypeID<LightTransparentPass>()] = CreateGlobalComponent<LightTransparentPass>();
        m_renderPassComponents[ID::GetTypeID<AabbsPass>()] = CreateGlobalComponent<AabbsPass>();

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(cmd);
        }

        // Init GUI
        m_gui.Init();

        m_renderArea.Update(0.0f, 0.0f, WIDTH_f, HEIGHT_f);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            ImageBarrierInfo barrierInfo{};
            barrierInfo.image = RHII.GetSwapchain()->GetImage(i);
            barrierInfo.layout = ImageLayout::PresentSrc;
            barrierInfo.stageFlags = PipelineStage::AllCommandsBit;
            barrierInfo.accessMask = Access::None;
            cmd->ImageBarrier(barrierInfo); // transition from undefined to present

            m_cmds[i] = nullptr;
        }
    }

    void RendererSystem::Update()
    {
        // Wait for the previous corresponding frame commands to finish first
        WaitPreviousFrameCommands();

        // GUI
        m_gui.Update();

        // Scene
        m_scene.Update();

        CameraSystem *cameraSystem = GetGlobalSystem<CameraSystem>();
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
        uint32_t frame = RHII.GetFrameIndex();

        auto &frameCmd = m_cmds[frame];
        if (frameCmd)
        {
            frameCmd->Wait();
            frameCmd->Return();
            frameCmd = nullptr;
        }

        RHII.ClaimUsedBinarySemaphores(frame);
    }

    void RendererSystem::Draw()
    {
        uint32_t frame = RHII.GetFrameIndex();

        // acquire the image
        Semaphore *aquireSignalSemaphore = RHII.GetFreeBinarySemaphore(frame);
        Swapchain *swapchain = RHII.GetSwapchain();
        uint32_t imageIndex = swapchain->Aquire(aquireSignalSemaphore);


        // RECORD COMMANDS
        auto &frameCmd = m_cmds[frame];
        frameCmd = RecordPasses(imageIndex);

        // SUBMIT TO QUEUE
        aquireSignalSemaphore->SetStageFlags(PipelineStage::ColorAttachmentOutputBit | PipelineStage::ComputeShaderBit);
        Semaphore *signal = RHII.GetFreeBinarySemaphore(frame);
        signal->SetStageFlags(PipelineStage::AllCommandsBit);

        Queue *queue = RHII.GetMainQueue();
        queue->Submit(1, &frameCmd, aquireSignalSemaphore, signal);

        // PRESENT
        queue->Present(swapchain, imageIndex, signal);
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
            rc->Destroy();

        m_skyBoxDay.Destroy();
        m_skyBoxNight.Destroy();

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);

        for (auto &dt : m_depthStencilTargets)
            Image::Destroy(dt.second);
    }
}
