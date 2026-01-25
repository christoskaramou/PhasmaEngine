#include "RendererSystem.h"
#include "API/Command.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Shader.h"
#include "API/StagingManager.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/ParticleComputePass.h"
#include "RenderPasses/ParticlePass.h"
#include "RenderPasses/RayTracingPass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/ShadowPass.h"
#include "RenderPasses/SharpenPass.h"
#include "RenderPasses/TAAPass.h"
#include "RenderPasses/TonemapPass.h"

namespace pe
{
    void RendererSystem::LoadResources(CommandBuffer *cmd)
    {
        m_skyBoxDay.LoadSkyBox(cmd, Path::Assets + "/Skyboxes/golden_gate_hills/golden_gate_hills_4k.hdr");
        m_skyBoxNight.LoadSkyBox(cmd, Path::Assets + "/Skyboxes/rogland_clear_night/rogland_clear_night_4k.hdr");
        m_skyBoxWhite.LoadSkyBox(cmd, Path::Assets + "/Objects/white_furnace_64x64.hdr");

        Image::LoadRawParams loadImageParams = {256, 256, vk::Format::eR16G16Sfloat, false, true, 0.0f};
        m_ibl_brdf_lut = Image::LoadRaw(cmd, Path::Assets + "Objects/ibl_brdf_lut_rg16f_256.bin", loadImageParams);
    }

    void RendererSystem::Init(CommandBuffer *cmd)
    {
        // Set Window Title
        std::string title = "PhasmaEngine";
        title += " - Device: " + RHII.GetGpuName();
        title += " - API: Vulkan";
        title += " - Present Mode: " + std::string(RHII.PresentModeToString(RHII.GetSurface()->GetPresentMode()));
#if PE_DEBUG
        title += " - Debug";
#elif PE_RELEASE
        title += " - Release";
#elif PE_MINSIZEREL
        title += " - MinSizeRel";
#elif PE_RELWITHDEBINFO
        title += " - RelWithDebInfo";
#endif

        EventSystem::DispatchEvent(EventType::SetWindowTitle, title);

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
        m_renderPassComponents[ID::GetTypeID<ParticleComputePass>()] = CreateGlobalComponent<ParticleComputePass>();
        m_renderPassComponents[ID::GetTypeID<ParticlePass>()] = CreateGlobalComponent<ParticlePass>();
        m_renderPassComponents[ID::GetTypeID<TAAPass>()] = CreateGlobalComponent<TAAPass>();
        m_renderPassComponents[ID::GetTypeID<SharpenPass>()] = CreateGlobalComponent<SharpenPass>();
        m_renderPassComponents[ID::GetTypeID<RayTracingPass>()] = CreateGlobalComponent<RayTracingPass>();

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(cmd);
        }

        // Init GUI
        m_gui.Init();

        uint32_t imageCount = RHII.GetSwapchainImageCount();
        m_cmds.resize(imageCount, nullptr);
        for (uint32_t i = 0; i < imageCount; i++)
        {
            ImageBarrierInfo barrierInfo{};
            barrierInfo.image = RHII.GetSwapchain()->GetImage(i);
            barrierInfo.layout = vk::ImageLayout::ePresentSrcKHR;
            barrierInfo.stageFlags = vk::PipelineStageFlagBits2::eAllCommands;
            barrierInfo.accessMask = vk::AccessFlagBits2::eNone;
            cmd->ImageBarrier(barrierInfo); // transition from undefined to present
        }
        m_acquireSemaphores.reserve(imageCount);
        m_submitSemaphores.reserve(imageCount);
        for (uint32_t i = 0; i < imageCount; i++)
        {
            Semaphore *acquireSemaphore = Semaphore::Create(false, "AcquireSemaphore_" + std::to_string(i));
            acquireSemaphore->SetStageFlags(vk::PipelineStageFlagBits2::eColorAttachmentOutput | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eTransfer);
            m_acquireSemaphores.push_back(acquireSemaphore);

            Semaphore *submitSemaphore = Semaphore::Create(false, "SubmitSemaphore_" + std::to_string(i));
            submitSemaphore->SetStageFlags(vk::PipelineStageFlagBits2::eAllCommands);
            m_submitSemaphores.push_back(submitSemaphore);
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

        // Render Components
        std::vector<std::shared_future<void>> futures;
        futures.reserve(m_renderPassComponents.size());
        for (auto &rc : m_renderPassComponents)
            futures.push_back(ThreadPool::Update.Enqueue([rc]()
                                                         { rc->Update(); }));

        for (auto &future : futures)
            future.wait();
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

        RHII.GetStagingManager()->RemoveUnused();
    }

    CommandBuffer *RendererSystem::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();

        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        DepthPass &dp = *GetGlobalComponent<DepthPass>();
        GbufferOpaquePass &gbo = *GetGlobalComponent<GbufferOpaquePass>();
        GbufferTransparentPass &gbt = *GetGlobalComponent<GbufferTransparentPass>();
        LightOpaquePass &lo = *GetGlobalComponent<LightOpaquePass>();
        LightTransparentPass &lt = *GetGlobalComponent<LightTransparentPass>();
        SSAOPass &ssao = *GetGlobalComponent<SSAOPass>();
        SSRPass &ssr = *GetGlobalComponent<SSRPass>();
        FXAAPass &fxaa = *GetGlobalComponent<FXAAPass>();
        BloomBrightFilterPass &bbfp = *GetGlobalComponent<BloomBrightFilterPass>();
        BloomGaussianBlurHorizontalPass &bgbh = *GetGlobalComponent<BloomGaussianBlurHorizontalPass>();
        BloomGaussianBlurVerticalPass &bgbv = *GetGlobalComponent<BloomGaussianBlurVerticalPass>();
        DOFPass &dof = *GetGlobalComponent<DOFPass>();
        MotionBlurPass &motionBlur = *GetGlobalComponent<MotionBlurPass>();
        TAAPass &taa = *GetGlobalComponent<TAAPass>();
        SharpenPass &sharpen = *GetGlobalComponent<SharpenPass>();
        TonemapPass &tonemap = *GetGlobalComponent<TonemapPass>();
        AabbsPass &aabbs = *GetGlobalComponent<AabbsPass>();
        ParticleComputePass &pcp = *GetGlobalComponent<ParticleComputePass>();
        ParticlePass &pp = *GetGlobalComponent<ParticlePass>();
        RayTracingPass &rtp = *GetGlobalComponent<RayTracingPass>();

        auto &gSettings = Settings::Get<GlobalSettings>();

        cmd->Begin();

        // Check dependencies
        bool renderRaster = !gSettings.use_ray_tracing;
        bool renderPostProcess = true; // Mostly always needed unless completely disabled

        bool renderShadows = gSettings.shadows && renderRaster;
        bool renderSSAO = gSettings.ssao && renderRaster;
        bool renderSSR = gSettings.ssr && renderRaster;
        bool renderParticles = true; // Always on top

        // Depth/GBuffer needed if Raster is on OR if needed by PostProcess/Particles
        bool needVelocity = gSettings.taa || gSettings.motion_blur;
        bool needDepth = renderRaster || renderParticles || gSettings.dof || gSettings.motion_blur || needVelocity;
        bool needGBuffer = renderRaster || needVelocity || renderSSR || renderSSAO;

        // Shadows Opaque
        if (renderShadows)
        {
            shadows.SetScene(&m_scene);
            shadows.ExecutePass(cmd);
        }

        // Depth Pass
        if (needDepth)
        {
            dp.SetScene(&m_scene);
            dp.ExecutePass(cmd);
        }

        // Gbuffers Opaque
        if (needGBuffer)
        {
            gbo.SetScene(&m_scene);
            gbo.ExecutePass(cmd);
        }

        if (renderRaster)
        {
            // Screen Space Ambient Occlusion
            if (renderSSAO)
            {
                ssao.ExecutePass(cmd);
            }

            {
                // Lighting Opaque
                {
                    lo.ExecutePass(cmd);
                }

                // Gbuffers Transparent
                {
                    gbt.SetScene(&m_scene);
                    gbt.ExecutePass(cmd);
                }

                // Lighting Transparent
                {
                    lt.ExecutePass(cmd);
                }
            }
        }
        else
        {
            // Ray Tracing Replaces Lighting
            rtp.SetScene(&m_scene);
            rtp.ExecutePass(cmd);
        }

        // Particle Passes (Draw on top)
        if (renderParticles)
        {
            pcp.SetScene(&m_scene);
            pcp.ExecutePass(cmd);

            pp.SetScene(&m_scene);
            pp.ExecutePass(cmd);
        }

        // Screen Space Reflections
        if (renderSSR)
        {
            ssr.ExecutePass(cmd);
        }

        // Post Process
        if (renderPostProcess)
        {
            // Fast Approximate Anti-Aliasing
            if (gSettings.fxaa)
            {
                fxaa.ExecutePass(cmd);
            }

            // Aabbs
            if (gSettings.draw_aabbs)
            {
                aabbs.SetScene(&m_scene);
                aabbs.ExecutePass(cmd);
            }

            // Upscale / TAA
            if (gSettings.taa)
            {
                taa.ExecutePass(cmd);

                // RCAS Sharpening
                if (gSettings.cas_sharpening)
                {
                    sharpen.ExecutePass(cmd);
                }
            }
            else
            {
                Upsample(cmd, vk::Filter::eLinear);
            }

            // Tone Mapping
            if (gSettings.tonemapping)
            {
                tonemap.ExecutePass(cmd);
            }

            // Bloom
            if (gSettings.bloom)
            {
                bbfp.ExecutePass(cmd);
                bgbh.ExecutePass(cmd);
                bgbv.ExecutePass(cmd);
            }

            // Depth of Field
            if (gSettings.dof)
            {
                dof.ExecutePass(cmd);
            }

            // Motion Blur
            if (gSettings.motion_blur)
            {
                motionBlur.ExecutePass(cmd);
            }
        }

        // Gui
        {
            m_gui.ExecutePass(cmd);
        }

        // Blit to swapchain
        {
            // Image *blitImage = gSettings.current_rendering_image ? gSettings.current_rendering_image : m_displayRT;
            BlitToSwapchain(cmd, m_displayRT, imageIndex);
        }

        cmd->End();

        return cmd;
    }

    void RendererSystem::Draw()
    {
        uint32_t frame = RHII.GetFrameIndex();

        // acquire the image
        Semaphore *acquireSemaphore = m_acquireSemaphores[frame];

        Swapchain *swapchain = RHII.GetSwapchain();
        uint32_t imageIndex = swapchain->AquireNextImage(acquireSemaphore);

        // RECORD COMMANDS
        auto &frameCmd = m_cmds[frame];
        frameCmd = RecordPasses(imageIndex);

        // SUBMIT TO QUEUE
        Semaphore *submitSemaphore = m_submitSemaphores[imageIndex];
        Queue *queue = RHII.GetMainQueue();
        queue->Submit(1, &frameCmd, acquireSemaphore, submitSemaphore);

        // PRESENT
        queue->Present(swapchain, imageIndex, submitSemaphore);
    }

    void RendererSystem::DrawPlatformWindows()
    {
        m_gui.DrawPlatformWindows();
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        for (auto &cmd : m_cmds)
        {
            if (cmd)
            {
                cmd->Wait();
                cmd->Return();
                cmd = nullptr;
            }
        }

        for (auto &rc : m_renderPassComponents)
            rc->Destroy();

        m_skyBoxDay.Destroy();
        m_skyBoxNight.Destroy();
        m_skyBoxWhite.Destroy();
        Image::Destroy(m_ibl_brdf_lut);

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);

        for (auto &dt : m_depthStencilTargets)
            Image::Destroy(dt.second);

        for (auto &semaphore : m_acquireSemaphores)
            Semaphore::Destroy(semaphore);

        for (auto &semaphore : m_submitSemaphores)
            Semaphore::Destroy(semaphore);
    }

    void RendererSystem::Upsample(CommandBuffer *cmd, vk::Filter filter)
    {
        vk::ImageBlit region{};
        region.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        region.srcOffsets[1] = vk::Offset3D{static_cast<int32_t>(m_viewportRT->GetWidth()), static_cast<int32_t>(m_viewportRT->GetHeight()), 1};
        region.srcSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_viewportRT->GetFormat());
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        region.dstOffsets[1] = vk::Offset3D{static_cast<int32_t>(m_displayRT->GetWidth()), static_cast<int32_t>(m_displayRT->GetHeight()), 1};
        region.dstSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_displayRT->GetFormat());
        region.dstSubresource.layerCount = 1;

        cmd->BlitImage(m_viewportRT, m_displayRT, region, filter);
    }

    Image *RendererSystem::CreateRenderTarget(const std::string &name,
                                              vk::Format format,
                                              vk::ImageUsageFlags usage,
                                              bool useRenderTergetScale,
                                              bool useMips,
                                              vec4 clearColor)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        uint32_t width = static_cast<uint32_t>(RHII.GetWidthf() * rtScale);
        uint32_t heigth = static_cast<uint32_t>(RHII.GetHeightf() * rtScale);

        vk::ImageCreateInfo info = Image::CreateInfoInit();
        info.format = format;
        info.extent = vk::Extent3D{width, heigth, 1u};
        info.usage = usage | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst;
        if (useMips)
            info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(width > heigth ? width : heigth))) + 1;
        Image *rt = Image::Create(info, name);
        rt->SetClearColor(clearColor);

        rt->CreateRTV();
        rt->CreateSRV(vk::ImageViewType::e2D);
        rt->CreateUAV(vk::ImageViewType::e2D, 0);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.anisotropyEnable = VK_FALSE;
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        rt->SetSampler(sampler);

        gSettings.rendering_images.push_back(rt);
        m_renderTargets[StringHash(name)] = rt;

        return rt;
    }

    Image *RendererSystem::CreateDepthStencilTarget(const std::string &name,
                                                    vk::Format format,
                                                    vk::ImageUsageFlags usage,
                                                    bool useRenderTergetScale,
                                                    float clearDepth,
                                                    uint32_t clearStencil)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        vk::ImageCreateInfo info = Image::CreateInfoInit();
        info.extent = vk::Extent3D{static_cast<uint32_t>(RHII.GetWidthf() * rtScale), static_cast<uint32_t>(RHII.GetHeightf() * rtScale), 1u};
        info.usage = usage | vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        info.format = format;
        Image *depth = Image::Create(info, name);
        depth->SetClearColor(vec4(clearDepth, static_cast<float>(clearStencil), 0.f, 0.f));

        depth->CreateRTV();
        depth->CreateSRV(vk::ImageViewType::e2D);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.compareEnable = VK_TRUE;
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        depth->SetSampler(sampler);

        gSettings.rendering_images.push_back(depth);
        m_depthStencilTargets[StringHash(name)] = depth;

        return depth;
    }

    Image *RendererSystem::GetRenderTarget(const std::string &name)
    {
        return m_renderTargets[StringHash(name)];
    }

    Image *RendererSystem::GetRenderTarget(size_t hash)
    {
        return m_renderTargets[hash];
    }

    Image *RendererSystem::GetDepthStencilTarget(const std::string &name)
    {
        return m_depthStencilTargets[StringHash(name)];
    }

    Image *RendererSystem::GetDepthStencilTarget(size_t hash)
    {
        return m_depthStencilTargets[hash];
    }

    Image *RendererSystem::CreateFSSampledImage(bool useRenderTergetScale)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        vk::ImageCreateInfo info = Image::CreateInfoInit();
        info.format = RHII.GetSurface()->GetFormat();
        info.extent = vk::Extent3D{static_cast<uint32_t>(RHII.GetWidthf() * rtScale), static_cast<uint32_t>(RHII.GetHeightf() * rtScale), 1u};
        info.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        Image *sampledImage = Image::Create(info, "FSSampledImage");

        sampledImage->CreateSRV(vk::ImageViewType::e2D);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        Sampler *sampler = Sampler::Create(samplerInfo, "FSSampledImage_sampler");
        sampledImage->SetSampler(sampler);

        return sampledImage;
    }

    void RendererSystem::CreateRenderTargets()
    {
        for (auto &framebuffer : CommandBuffer::GetFramebuffers())
            Framebuffer::Destroy(framebuffer.second);
        CommandBuffer::GetFramebuffers().clear();

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);
        m_renderTargets.clear();

        for (auto &rt : m_depthStencilTargets)
            Image::Destroy(rt.second);
        m_depthStencilTargets.clear();

        Settings::Get<GlobalSettings>().rendering_images.clear();

        vk::Format surfaceFormat = RHII.GetSurface()->GetFormat();
        m_depthStencil = CreateDepthStencilTarget("depthStencil", RHII.GetDepthFormat(), vk::ImageUsageFlagBits::eTransferDst);
        m_viewportRT = CreateRenderTarget("viewport", surfaceFormat, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
        m_displayRT = CreateRenderTarget("display", surfaceFormat, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst, false);
        CreateRenderTarget("normal", vk::Format::eR16G16B16A16Sfloat);
        CreateRenderTarget("albedo", surfaceFormat);
        CreateRenderTarget("srm", surfaceFormat); // Specular Roughness Metallic
        CreateRenderTarget("ssao", vk::Format::eR8Unorm);
        CreateRenderTarget("ssr", surfaceFormat);
        CreateRenderTarget("velocity", vk::Format::eR16G16Sfloat);
        CreateRenderTarget("emissive", surfaceFormat);
        CreateRenderTarget("brightFilter", surfaceFormat, {}, false);
        CreateRenderTarget("gaussianBlurHorizontal", surfaceFormat, {}, false);
        CreateRenderTarget("gaussianBlurVertical", surfaceFormat, {}, false);
        CreateRenderTarget("transparency", vk::Format::eR8Unorm, {}, true, false, Color::Black);
    }

    void RendererSystem::Resize(uint32_t width, uint32_t height)
    {
        // Wait idle, we dont want to destroy objects in use
        RHII.WaitDeviceIdle();
        RHII.GetSurface()->SetActualExtent({0, 0, width, height});

        Swapchain *swapchain = RHII.GetSwapchain();
        Swapchain::Destroy(swapchain);

        Surface *surface = RHII.GetSurface();
        RHII.CreateSwapchain(surface);

        CreateRenderTargets();

        for (auto &rc : m_renderPassComponents)
            rc->Resize(width, height);
    }

    void RendererSystem::BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex)
    {
        Image *swapchainImage = RHII.GetSwapchain()->GetImage(imageIndex);

        vk::ImageBlit region{};
        region.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        region.srcOffsets[1] = vk::Offset3D{static_cast<int32_t>(src->GetWidth()), static_cast<int32_t>(src->GetHeight()), 1};
        region.srcSubresource.aspectMask = VulkanHelpers::GetAspectMask(src->GetFormat());
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        region.dstOffsets[1] = vk::Offset3D{(int32_t)src->GetWidth(), (int32_t)src->GetHeight(), 1};
        region.dstSubresource.aspectMask = VulkanHelpers::GetAspectMask(swapchainImage->GetFormat());
        region.dstSubresource.layerCount = 1;

        ImageBarrierInfo barrier{};
        barrier.image = swapchainImage;
        barrier.layout = vk::ImageLayout::ePresentSrcKHR;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eAllCommands;
        barrier.accessMask = vk::AccessFlagBits2::eNone;

        // with 1:1 ratio we can use nearest filter
        vk::Filter filter = src->GetWidth() == swapchainImage->GetWidth() && src->GetHeight() == swapchainImage->GetHeight() ? vk::Filter::eNearest : vk::Filter::eLinear;
        cmd->BlitImage(src, swapchainImage, region, filter);
        cmd->ImageBarrier(barrier);
    }

    void RendererSystem::PollShaders()
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
