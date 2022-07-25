#include "RendererSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Model/Mesh.h"
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
        Format format = surface->format;

        // SET WINDOW TITLE
        std::string title = "PhasmaEngine";
        title += " - Device: " + RHII.GetGpuName();
        title += " - API: Vulkan";
        title += " - Present Mode: " + PresentModeToString(surface->presentMode);
#ifdef _DEBUG
        title += " - Configuration: Debug";
#else
        title += " - Configuration: Release";
#endif // _DEBUG

        EventSystem::DispatchEvent(EventSetWindowTitle, title);

        m_depth = CreateDepthTarget("depth", RHII.GetDepthFormat(), ImageUsage::DepthStencilAttachmentBit | ImageUsage::SampledBit);
        m_viewportRT = CreateRenderTarget("viewport", format, false, ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit);
        m_displayRT = CreateRenderTarget("display", format, false, ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit | ImageUsage::StorageBit, false);
        CreateRenderTarget("normal", Format::RGBA32SFloat, false);
        CreateRenderTarget("albedo", format, true);
        CreateRenderTarget("srm", format, false); // Specular Roughness Metallic
        CreateRenderTarget("ssao", Format::R8Unorm, false);
        CreateRenderTarget("ssaoBlur", Format::R8Unorm, false);
        CreateRenderTarget("ssr", format, false);
        CreateRenderTarget("velocity", Format::RG16SFloat, false);
        CreateRenderTarget("emissive", format, false);
        CreateRenderTarget("brightFilter", format, false, {}, false);
        CreateRenderTarget("gaussianBlurHorizontal", format, false, {}, false);
        CreateRenderTarget("gaussianBlurVertical", format, false, {}, false);

        // LOAD RESOURCES
        LoadResources(cmd);
        // CREATE UNIFORMS AND DESCRIPTOR SETS
        CreateUniforms();

        // INIT render components
        m_renderComponents[GetTypeID<Shadows>()] = WORLD_ENTITY->CreateComponent<Shadows>();
        m_renderComponents[GetTypeID<Deferred>()] = WORLD_ENTITY->CreateComponent<Deferred>();
        for (auto &renderComponent : m_renderComponents)
        {
            renderComponent.second->Init();
            renderComponent.second->CreateUniforms(cmd);
            renderComponent.second->UpdatePipelineInfo();
        }

        // GUI LOAD
        gui.CreateRenderPass();
        gui.CreateFrameBuffers();
        gui.InitGUI();

        renderArea.Update(0.0f, 0.0f, WIDTH_f, HEIGHT_f);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_previousCmds[i] = nullptr;
            for (uint32_t j = 0; j < SHADOWMAP_CASCADES; j++)
                m_previousShadowCmds[i][j] = nullptr;
        }

        // GPU TIMERS
        GpuTimer::gpu = GpuTimer::Create("GPUTimer_queryPool_gpu");
        GpuTimer::compute = GpuTimer::Create("GPUTimer_queryPool_compute");
        GpuTimer::geometry = GpuTimer::Create("GPUTimer_queryPool_geometry");
        GpuTimer::ssao = GpuTimer::Create("GPUTimer_queryPool_ssao");
        GpuTimer::ssr = GpuTimer::Create("GPUTimer_queryPool_ssr");
        GpuTimer::composition = GpuTimer::Create("GPUTimer_queryPool_composition");
        GpuTimer::fxaa = GpuTimer::Create("GPUTimer_queryPool_fxaa");
        GpuTimer::bloom = GpuTimer::Create("GPUTimer_queryPool_bloom");
        GpuTimer::ssgi = GpuTimer::Create("GPUTimer_queryPool_ssgi");
        GpuTimer::dof = GpuTimer::Create("GPUTimer_queryPool_dof");
        GpuTimer::motionBlur = GpuTimer::Create("GPUTimer_queryPool_motionBlur");
        GpuTimer::gui = GpuTimer::Create("GPUTimer_queryPool_gui");
        GpuTimer::fsr = GpuTimer::Create("GPUTimer_queryPool_fsr");
        for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
            GpuTimer::shadows[i] = GpuTimer::Create("GPUTimer_queryPool_shadows_" + std::to_string(i));
    }

    void RendererSystem::Update(double delta)
    {
#ifndef IGNORE_SCRIPTS
        // universal scripts
        for (auto &s : scripts)
            s->update(static_cast<float>(delta));
#endif

        CameraSystem *cameraSystem = CONTEXT->GetSystem<CameraSystem>();
        Camera *camera_main = cameraSystem->GetCamera(0);

        // MODELS
        if (GUI::modelItemSelected > -1)
        {
            Model::models[GUI::modelItemSelected].scale = make_vec3(GUI::model_scale[GUI::modelItemSelected].data());
            Model::models[GUI::modelItemSelected].pos = make_vec3(GUI::model_pos[GUI::modelItemSelected].data());
            Model::models[GUI::modelItemSelected].rot = make_vec3(GUI::model_rot[GUI::modelItemSelected].data());
        }
        for (auto &m : Model::models)
            m.Update(*camera_main, delta);

        // RENDER COMPONENTS
        for (auto &rc : m_renderComponents)
            rc.second->Update(camera_main);

        // GUI
        gui.Update();
    }

    void RendererSystem::Draw()
    {
        uint32_t frameIndex = RHII.GetFrameIndex();

        Queue *queue = RHII.GetRenderQueue();
        s_currentQueue = queue;

        queue->BeginDebugRegion("RendererSystem::Draw");

        static PipelineStageFlags waitStages[] = {
            PipelineStage::ColorAttachmentOutputBit,
            PipelineStage::FragmentShaderBit};

        // acquire the image
        auto aquireSignalSemaphore = RHII.GetSemaphores()[frameIndex];
        uint32_t imageIndex = RHII.GetSwapchain()->Aquire(aquireSignalSemaphore);

        static Timer timer;
        timer.Start();

        if (GUI::shadow_cast)
        {
            CommandBuffer *shadowCmds[SHADOWMAP_CASCADES];
            for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
            {
                // Wait for unfinished work
                if (m_previousShadowCmds[imageIndex][i])
                {
                    m_previousShadowCmds[imageIndex][i]->Wait();
                    CommandBuffer::Return(m_previousShadowCmds[imageIndex][i]);
                }

                shadowCmds[i] = CommandBuffer::GetNext(queue->GetFamilyId());
                m_previousShadowCmds[imageIndex][i] = shadowCmds[i];
            }

            // record the shadow command buffers
            queue->BeginDebugRegion("Record Shadow Cmds");
            RecordShadowsCmds(SHADOWMAP_CASCADES, shadowCmds, imageIndex);
            queue->EndDebugRegion();

            // submit the shadow command buffers
            auto &shadowWaitSemaphore = aquireSignalSemaphore;
            auto &shadowSignalSemaphore = RHII.GetSemaphores()[SWAPCHAIN_IMAGES + frameIndex];

            queue->Submit(
                SHADOWMAP_CASCADES, shadowCmds,
                &waitStages[0],
                1, &shadowWaitSemaphore,
                1, &shadowSignalSemaphore);

            aquireSignalSemaphore = shadowSignalSemaphore;
        }

        PipelineStageFlags waitStage = GUI::shadow_cast ? waitStages[1] : waitStages[0];
        Semaphore *waitSemaphore = aquireSignalSemaphore;
        Semaphore *signalSemaphore = RHII.GetSemaphores()[SWAPCHAIN_IMAGES * 2 + frameIndex];

        // Wait for unfinished work
        if (m_previousCmds[imageIndex])
        {
            m_previousCmds[imageIndex]->Wait();
            CommandBuffer::Return(m_previousCmds[imageIndex]);
        }

        // RECORD
        CommandBuffer *cmd = CommandBuffer::GetNext(queue->GetFamilyId());
        cmd->Begin();
        RecordPasses(cmd, imageIndex);
        cmd->End();

        // SUBMIT TO QUEUE
        queue->Submit(1, &cmd, &waitStage, 1, &waitSemaphore, 1, &signalSemaphore);

        m_previousCmds[imageIndex] = cmd;

        // PRESENT
        Swapchain *swapchain = RHII.GetSwapchain();
        Semaphore *presentWaitSemaphore = signalSemaphore;
        queue->Present(1, &swapchain, &imageIndex, 1, &presentWaitSemaphore);

        gui.RenderViewPorts();

        queue->EndDebugRegion();
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        GpuTimer::Destroy(GpuTimer::gpu);
        GpuTimer::Destroy(GpuTimer::compute);
        GpuTimer::Destroy(GpuTimer::geometry);
        GpuTimer::Destroy(GpuTimer::ssao);
        GpuTimer::Destroy(GpuTimer::ssr);
        GpuTimer::Destroy(GpuTimer::composition);
        GpuTimer::Destroy(GpuTimer::fxaa);
        GpuTimer::Destroy(GpuTimer::bloom);
        GpuTimer::Destroy(GpuTimer::ssgi);
        GpuTimer::Destroy(GpuTimer::dof);
        GpuTimer::Destroy(GpuTimer::motionBlur);
        GpuTimer::Destroy(GpuTimer::gui);
        GpuTimer::Destroy(GpuTimer::fsr);
        for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
            GpuTimer::Destroy(GpuTimer::shadows[i]);

        for (auto &rc : m_renderComponents)
            rc.second->Destroy();

#ifndef IGNORE_SCRIPTS
        for (auto &script : scripts)
            delete script;
#endif
        for (auto &model : Model::models)
            model.Destroy();
        for (auto &texture : Mesh::uniqueTextures)
            Image::Destroy(texture.second);
        Mesh::uniqueTextures.clear();

        Compute::DestroyResources();
        skyBoxDay.destroy();
        skyBoxNight.destroy();
        gui.Destroy();

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);
        for (auto &dt : m_depthTargets)
            Image::Destroy(dt.second);
    }
}
