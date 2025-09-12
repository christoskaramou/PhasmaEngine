#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "API/RHI.h"
#include "API/Framebuffer.h"
#include "API/Semaphore.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/TonemapPass.h"
#include "RenderPasses/ShadowPass.h"
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

    void RendererSystem::LoadResources(CommandBuffer *cmd)
    {
        // SKYBOXES LOAD
        std::array<std::string, 6> skyTextures = {
            Path::Executable + "Assets/Objects/sky/right.png",
            Path::Executable + "Assets/Objects/sky/left.png",
            Path::Executable + "Assets/Objects/sky/top.png",
            Path::Executable + "Assets/Objects/sky/bottom.png",
            Path::Executable + "Assets/Objects/sky/back.png",
            Path::Executable + "Assets/Objects/sky/front.png"};
        m_skyBoxDay.LoadSkyBox(cmd, skyTextures, 1024);
        skyTextures = {
            Path::Executable + "Assets/Objects/lmcity/lmcity_rt.png",
            Path::Executable + "Assets/Objects/lmcity/lmcity_lf.png",
            Path::Executable + "Assets/Objects/lmcity/lmcity_up.png",
            Path::Executable + "Assets/Objects/lmcity/lmcity_dn.png",
            Path::Executable + "Assets/Objects/lmcity/lmcity_bk.png",
            Path::Executable + "Assets/Objects/lmcity/lmcity_ft.png"};
        m_skyBoxNight.LoadSkyBox(cmd, skyTextures, 512);
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

        Camera *camera_main = GetGlobalSystem<CameraSystem>()->GetCamera(0);

        // Render Components
        std::vector<std::shared_future<void>> futures;
        futures.reserve(m_renderPassComponents.size());
        for (auto &rc : m_renderPassComponents)
            futures.push_back(e_Update_ThreadPool.Enqueue([rc, camera_main]()
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

        RHII.ReturnBinarySemaphores(frame);
    }

    CommandBuffer *RendererSystem::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();

        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        SSAOPass &ssao = *GetGlobalComponent<SSAOPass>();
        SSRPass &ssr = *GetGlobalComponent<SSRPass>();
        FXAAPass &fxaa = *GetGlobalComponent<FXAAPass>();
        BloomBrightFilterPass &bbfp = *GetGlobalComponent<BloomBrightFilterPass>();
        BloomGaussianBlurHorizontalPass &bgbh = *GetGlobalComponent<BloomGaussianBlurHorizontalPass>();
        BloomGaussianBlurVerticalPass &bgbv = *GetGlobalComponent<BloomGaussianBlurVerticalPass>();
        DOFPass &dof = *GetGlobalComponent<DOFPass>();
        MotionBlurPass &motionBlur = *GetGlobalComponent<MotionBlurPass>();
        SuperResolutionPass &sr = *GetGlobalComponent<SuperResolutionPass>();
        TonemapPass &tonemap = *GetGlobalComponent<TonemapPass>();

        auto &gSettings = Settings::Get<GlobalSettings>();

        cmd->Begin();

        // Shadows Opaque
        if (gSettings.shadows)
        {
            m_scene.DrawShadowPass(cmd);
        }

        // Depth Pass
        {
            m_scene.DepthPrePass(cmd);
        }

        // Gbuffers Opaque
        {
            m_scene.DrawGbufferPassOpaque(cmd);
        }

        // Screen Space Ambient Occlusion
        if (gSettings.ssao)
        {
            ssao.Draw(cmd);
        }

        // Lighting Opaque
        {
            m_scene.DrawLightPassOpaque(cmd);
        }

        // Gbuffers Transparent
        {
            m_scene.DrawGbufferPassTransparent(cmd);
        }

        // Lighting Transparent
        {
            m_scene.DrawLightPassTransparent(cmd);
        }

        // Screen Space Reflections
        if (gSettings.ssr)
        {
            ssr.Draw(cmd);
        }

        // Fast Approximate Anti-Aliasing
        if (gSettings.fxaa)
        {
            fxaa.Draw(cmd);
        }

        // Aabbs
        if (gSettings.draw_aabbs)
        {
            m_scene.DrawAabbsPass(cmd);
        }

        // Upscale
        if (Settings::Get<SRSettings>().enable)
        {
            // FidelityFX Super Resolution
            sr.Draw(cmd);
        }
        else
        {
            Upsample(cmd, Filter::Linear);
        }

        // Tone Mapping
        if (gSettings.tonemapping)
        {
            tonemap.Draw(cmd);
        }

        // Bloom
        if (gSettings.bloom)
        {
            bbfp.Draw(cmd);
            bgbh.Draw(cmd);
            bgbv.Draw(cmd);
        }

        // Depth of Field
        if (gSettings.dof)
        {
            dof.Draw(cmd);
        }

        // Motion Blur
        if (gSettings.motion_blur)
        {
            motionBlur.Draw(cmd);
        }

        // Gui
        {
            m_gui.Draw(cmd);
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
        Semaphore *aquireSignalSemaphore = RHII.AcquireBinarySemaphore(frame);
        Swapchain *swapchain = RHII.GetSwapchain();
        uint32_t imageIndex = swapchain->Aquire(aquireSignalSemaphore);

        // RECORD COMMANDS
        auto &frameCmd = m_cmds[frame];
        frameCmd = RecordPasses(imageIndex);

        // SUBMIT TO QUEUE
        aquireSignalSemaphore->SetStageFlags(PipelineStage::ColorAttachmentOutputBit | PipelineStage::ComputeShaderBit);
        Semaphore *signal = RHII.AcquireBinarySemaphore(frame);
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

    void RenderArea::Update(float x, float y, float w, float h, float minDepth, float maxDepth)
    {
        viewport.x = x;
        viewport.y = y;
        viewport.width = w;
        viewport.height = h;
        viewport.minDepth = minDepth;
        viewport.maxDepth = maxDepth;

        scissor.x = static_cast<int32_t>(x);
        scissor.y = static_cast<int32_t>(y);
        scissor.width = static_cast<int32_t>(w);
        scissor.height = static_cast<int32_t>(h);
    }

    void RendererSystem::Upsample(CommandBuffer *cmd, Filter filter)
    {
        // Blit viewportRT to displayRT with a filter
        Viewport &vp = m_renderArea.viewport;
        ImageBlit region{};
        region.srcOffsets[0] = Offset3D{0, 0, 0};
        region.srcOffsets[1] = Offset3D{static_cast<int32_t>(m_viewportRT->GetWidth()), static_cast<int32_t>(m_viewportRT->GetHeight()), 1};
        region.srcSubresource.aspectMask = GetAspectMask(m_viewportRT->GetFormat());
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[0] = Offset3D{static_cast<int32_t>(vp.x), static_cast<int32_t>(vp.y), 0};
        region.dstOffsets[1] = Offset3D{static_cast<int32_t>(vp.x) + static_cast<int32_t>(vp.width), static_cast<int32_t>(vp.y) + static_cast<int32_t>(vp.height), 1};
        region.dstSubresource.aspectMask = GetAspectMask(m_displayRT->GetFormat());
        region.dstSubresource.layerCount = 1;

        cmd->BlitImage(m_viewportRT, m_displayRT, &region, filter);
    }

    Image *RendererSystem::CreateRenderTarget(const std::string &name,
                                              Format format,
                                              ImageUsageFlags additionalFlags,
                                              bool useRenderTergetScale,
                                              bool useMips,
                                              vec4 clearColor)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        uint32_t width = static_cast<uint32_t>(WIDTH_f * rtScale);
        uint32_t heigth = static_cast<uint32_t>(HEIGHT_f * rtScale);

        ImageCreateInfo info{};
        info.format = format;
        info.width = width;
        info.height = heigth;
        info.usage = additionalFlags | ImageUsage::ColorAttachmentBit | ImageUsage::SampledBit | ImageUsage::StorageBit | ImageUsage::TransferDstBit;
        info.clearColor = clearColor;
        info.name = name;
        if (useMips)
            info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(width > heigth ? width : heigth))) + 1;
        Image *rt = Image::Create(info);

        rt->CreateRTV();
        rt->CreateSRV(ImageViewType::Type2D);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.anisotropyEnable = 0;
        rt->SetSampler(Sampler::Create(samplerInfo));

        gSettings.rendering_images.push_back(rt);
        m_renderTargets[StringHash(name)] = rt;

        return rt;
    }

    Image *RendererSystem::CreateDepthStencilTarget(const std::string &name,
                                                    Format format,
                                                    ImageUsageFlags additionalFlags,
                                                    bool useRenderTergetScale,
                                                    float clearDepth,
                                                    uint32_t clearStencil)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        ImageCreateInfo info{};
        info.width = static_cast<uint32_t>(WIDTH_f * rtScale);
        info.height = static_cast<uint32_t>(HEIGHT_f * rtScale);
        info.usage = additionalFlags | ImageUsage::DepthStencilAttachmentBit | ImageUsage::SampledBit | ImageUsage::TransferDstBit;
        info.format = format;
        info.clearColor = vec4(clearDepth, static_cast<float>(clearStencil), 0.f, 0.f);
        info.name = name;
        Image *depth = Image::Create(info);

        depth->CreateRTV();
        depth->CreateSRV(ImageViewType::Type2D);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.addressModeU = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeV = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeW = SamplerAddressMode::ClampToEdge;
        samplerInfo.anisotropyEnable = 0;
        samplerInfo.borderColor = BorderColor::FloatOpaqueWhite;
        samplerInfo.compareEnable = 1;
        depth->SetSampler(Sampler::Create(samplerInfo));

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

        ImageCreateInfo info{};
        info.format = RHII.GetSurface()->GetFormat();
        info.initialLayout = ImageLayout::Undefined;
        info.width = static_cast<uint32_t>(WIDTH_f * rtScale);
        info.height = static_cast<uint32_t>(HEIGHT_f * rtScale);
        info.tiling = ImageTiling::Optimal;
        info.usage = ImageUsage::TransferDstBit | ImageUsage::SampledBit;
        info.name = "FSSampledImage";
        Image *sampledImage = Image::Create(info);

        sampledImage->CreateSRV(ImageViewType::Type2D);

        SamplerCreateInfo samplerInfo{};
        sampledImage->SetSampler(Sampler::Create(samplerInfo));

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

        Format surfaceFormat = RHII.GetSurface()->GetFormat();
        m_depthStencil = CreateDepthStencilTarget("depthStencil", RHII.GetDepthFormat(), ImageUsage::TransferDstBit);
        m_viewportRT = CreateRenderTarget("viewport", surfaceFormat, ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit);
        m_displayRT = CreateRenderTarget("display", surfaceFormat, ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit, false);
        CreateRenderTarget("normal", Format::RGBA16SFloat);
        CreateRenderTarget("albedo", surfaceFormat);
        CreateRenderTarget("srm", surfaceFormat); // Specular Roughness Metallic
        CreateRenderTarget("ssao", Format::R8Unorm);
        CreateRenderTarget("ssr", surfaceFormat);
        CreateRenderTarget("velocity", Format::RG16SFloat);
        CreateRenderTarget("emissive", surfaceFormat);
        CreateRenderTarget("brightFilter", surfaceFormat, {}, false);
        CreateRenderTarget("gaussianBlurHorizontal", surfaceFormat, {}, false);
        CreateRenderTarget("gaussianBlurVertical", surfaceFormat, {}, false);
        CreateRenderTarget("transparency", Format::R8Unorm, {}, true, false, Color::Black);
    }

    void RendererSystem::Resize(uint32_t width, uint32_t height)
    {
        // Wait idle, we dont want to destroy objects in use
        RHII.WaitDeviceIdle();
        RHII.GetSurface()->SetActualExtent({0, 0, width, height});

        m_renderArea.Update(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

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
        Viewport &vp = m_renderArea.viewport;

        ImageBlit region{};
        region.srcOffsets[0] = Offset3D{0, 0, 0};
        region.srcOffsets[1] = Offset3D{static_cast<int32_t>(src->GetWidth()), static_cast<int32_t>(src->GetHeight()), 1};
        region.srcSubresource.aspectMask = GetAspectMask(src->GetFormat());
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[0] = Offset3D{(int32_t)vp.x, (int32_t)vp.y, 0};
        region.dstOffsets[1] = Offset3D{(int32_t)vp.x + (int32_t)vp.width, (int32_t)vp.y + (int32_t)vp.height, 1};
        region.dstSubresource.aspectMask = GetAspectMask(swapchainImage->GetFormat());
        region.dstSubresource.layerCount = 1;

        ImageBarrierInfo barrier{};
        barrier.image = swapchainImage;
        barrier.layout = ImageLayout::PresentSrc;
        barrier.stageFlags = PipelineStage::AllCommandsBit;
        barrier.accessMask = Access::None;

        // with 1:1 ratio we can use nearest filter
        Filter filter = src->GetWidth() == (uint32_t)vp.width && src->GetHeight() == (uint32_t)vp.height ? Filter::Nearest : Filter::Linear;
        cmd->BlitImage(src, swapchainImage, &region, filter);
        cmd->ImageBarrier(barrier);
    }

    void RendererSystem::PollShaders()
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
        {
            std::shared_ptr<PassInfo> info = rc->GetPassInfo();

            // PollEvent simply catches a pushed event from FileWatcher
            if (info->pCompShader && EventSystem::PollEvent(info->pCompShader->GetPathID()) ||
                info->pVertShader && EventSystem::PollEvent(info->pVertShader->GetPathID()) ||
                info->pFragShader && EventSystem::PollEvent(info->pFragShader->GetPathID()))
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
