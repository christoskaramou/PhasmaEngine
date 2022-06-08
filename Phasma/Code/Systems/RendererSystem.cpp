/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "RendererSystem.h"
#include "Systems/EventSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Queue.h"
#include "Renderer/Fence.h"
#include "Renderer/Command.h"
#include "Model/Mesh.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
    std::string PresentModeToString(PresentMode presentModeKHR)
    {
        switch (presentModeKHR)
        {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "Immediate";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "Mailbox";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "Fifo";
        default:
            return "";
        }
    }

    void RendererSystem::Init()
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

        CONTEXT->GetSystem<EventSystem>()->DispatchEvent(EventType::SetWindowTitle, title);

        m_viewportRT = CreateRenderTarget("viewport", format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        m_depth = CreateDepthTarget("depth", RHII.GetDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        CreateRenderTarget("normal", VK_FORMAT_R32G32B32A32_SFLOAT);
        CreateRenderTarget("albedo", format);
        CreateRenderTarget("srm", format); // Specular Roughness Metallic
        CreateRenderTarget("ssao", VK_FORMAT_R16_UNORM);
        CreateRenderTarget("ssaoBlur", VK_FORMAT_R8_UNORM);
        CreateRenderTarget("ssr", format);
        CreateRenderTarget("velocity", VK_FORMAT_R16G16_SFLOAT);
        CreateRenderTarget("brightFilter", format);
        CreateRenderTarget("gaussianBlurHorizontal", format);
        CreateRenderTarget("gaussianBlurVertical", format);
        CreateRenderTarget("emissive", format);
        CreateRenderTarget("taa", format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

        // INIT render components
        m_renderComponents[GetTypeID<Deferred>()] = WORLD_ENTITY->CreateComponent<Deferred>();
        m_renderComponents[GetTypeID<Shadows>()] = WORLD_ENTITY->CreateComponent<Shadows>();
        for (auto &renderComponent : m_renderComponents)
        {
            renderComponent.second->Init();
            renderComponent.second->CreateRenderPass();
            renderComponent.second->CreateFrameBuffers();
            renderComponent.second->CreatePipeline();
            renderComponent.second->CreateUniforms();
        }

        // LOAD RESOURCES
        LoadResources();
        // CREATE UNIFORMS AND DESCRIPTOR SETS
        CreateUniforms();

        // GUI LOAD
        gui.CreateRenderPass();
        gui.CreateFrameBuffers();
        gui.InitGUI();

        renderArea.Update(0.0f, 0.0f, WIDTH_f, HEIGHT_f);
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
            Model::models[GUI::modelItemSelected].scale = vec3(GUI::model_scale[GUI::modelItemSelected].data());
            Model::models[GUI::modelItemSelected].pos = vec3(GUI::model_pos[GUI::modelItemSelected].data());
            Model::models[GUI::modelItemSelected].rot = vec3(GUI::model_rot[GUI::modelItemSelected].data());
        }
        for (auto &m : Model::models)
            m.update(*camera_main, delta);
        // SyncQueue<Launch::Async>::Request([&m, camera_main, delta]()
        //                               { m.update(*camera_main, delta); });

        // RENDER COMPONENTS
        for (auto &rc : m_renderComponents)
            rc.second->Update(camera_main);
        // SyncQueue<Launch::Async>::Request([camera_main, rc]()
        //                               { rc.second->Update(camera_main); });

        // GUI
        gui.Update();
        // SyncQueue<Launch::Async>::Request([this]()
        //                               { gui.Update(); });
    }

    void RendererSystem::Draw()
    {
        Queue *queue = Queue::GetNext(QueueType::GraphicsBit, 1);
        s_currentQueue = queue;
        CommandBuffer *cmd = CommandBuffer::GetNext(queue->GetFamilyId());

        Debug::BeginQueueRegion(queue, "RendererSystem::Draw");

        uint32_t frameIndex = RHII.GetFrameIndex();

        static VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};

        // acquire the image
        auto aquireSignalSemaphore = RHII.GetSemaphores()[frameIndex];
        uint32_t imageIndex = RHII.GetSwapchain()->Aquire(aquireSignalSemaphore, nullptr);

        static Timer timer;
        timer.Start();

        if (previousFences[imageIndex])
        {
            previousFences[imageIndex]->Wait();
            previousFences[imageIndex]->Reset();
            Fence::Return(previousFences[imageIndex]);
        }

        FrameTimer::Instance().timestamps[1] = timer.Count();

        if (GUI::shadow_cast)
        {
            CommandBuffer *shadowCmds[SHADOWMAP_CASCADES];
            for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
                shadowCmds[i] = CommandBuffer::GetNext(queue->GetFamilyId());

            // record the shadow command buffers
            Debug::BeginQueueRegion(queue, "RecordDeferredCmds");
            RecordShadowsCmds(shadowCmds, SHADOWMAP_CASCADES, imageIndex);
            Debug::EndQueueRegion(queue);

            // submit the shadow command buffers
            auto &shadowWaitSemaphore = aquireSignalSemaphore;
            auto &shadowSignalSemaphore = RHII.GetSemaphores()[imageIndex * 3 + 1];

            queue->Submit(
                SHADOWMAP_CASCADES, shadowCmds,
                &waitStages[0],
                1, &shadowWaitSemaphore,
                1, &shadowSignalSemaphore,
                nullptr);

            for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
                CommandBuffer::Return(shadowCmds[i]);

            aquireSignalSemaphore = shadowSignalSemaphore;
        }

        // record the command buffers
        RecordDeferredCmds(cmd, imageIndex);

        // submit the command buffers
        auto waitStage = GUI::shadow_cast ? waitStages[1] : waitStages[0];
        auto waitSemaphore = aquireSignalSemaphore;
        auto signalSemaphore = RHII.GetSemaphores()[imageIndex * 3 + 2];

        Fence *fence = Fence::GetNext();
        previousFences[imageIndex] = fence;

        queue->Submit(
            1, &cmd,
            &waitStage,
            1, &waitSemaphore,
            1, &signalSemaphore,
            fence);

        // Presentation
        auto presentWaitSemaphore = signalSemaphore;
        Queue *presentQueue = Queue::GetNext(QueueType::PresentBit, 1);
        Swapchain *swapchain = RHII.GetSwapchain();
        presentQueue->Present(1, &swapchain, &imageIndex, 1, &presentWaitSemaphore);
        Queue::Return(presentQueue);

        gui.RenderViewPorts();

        frameIndex = (frameIndex + 1) % SWAPCHAIN_IMAGES;

        Debug::EndQueueRegion(queue);

        CommandBuffer::Return(cmd);
        Queue::Return(queue);
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderComponents)
            rc.second->Destroy();

#ifndef IGNORE_SCRIPTS
        for (auto &script : scripts)
            delete script;
#endif
        for (auto &model : Model::models)
            model.destroy();
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
