#include "Renderer/Renderer.h"
#include "Renderer/RHI.h"
#include "Renderer/Surface.h"
#include "Renderer/Command.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Queue.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Shader.h"

namespace pe
{
    Renderer::Renderer()
    {
    }

    Renderer::~Renderer()
    {
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

    void Renderer::Upsample(CommandBuffer *cmd, Filter filter)
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

    Image *Renderer::CreateRenderTarget(const std::string &name,
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
        rt->m_sampler = Sampler::Create(samplerInfo);

        gSettings.rendering_images.push_back(rt);
        m_renderTargets[StringHash(name)] = rt;

        return rt;
    }

    Image *Renderer::CreateDepthStencilTarget(const std::string &name,
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
        depth->m_sampler = Sampler::Create(samplerInfo);

        gSettings.rendering_images.push_back(depth);
        m_depthStencilTargets[StringHash(name)] = depth;

        return depth;
    }

    Image *Renderer::GetRenderTarget(const std::string &name)
    {
        return m_renderTargets[StringHash(name)];
    }

    Image *Renderer::GetRenderTarget(size_t hash)
    {
        return m_renderTargets[hash];
    }

    Image *Renderer::GetDepthStencilTarget(const std::string &name)
    {
        return m_depthStencilTargets[StringHash(name)];
    }

    Image *Renderer::GetDepthStencilTarget(size_t hash)
    {
        return m_depthStencilTargets[hash];
    }

    Image *Renderer::CreateFSSampledImage(bool useRenderTergetScale)
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
        sampledImage->m_sampler = Sampler::Create(samplerInfo);

        return sampledImage;
    }

    void Renderer::CreateRenderTargets()
    {
        for (auto &framebuffer : CommandBuffer::s_framebuffers)
            Framebuffer::Destroy(framebuffer.second);
        CommandBuffer::s_framebuffers.clear();

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

    void Renderer::Resize(uint32_t width, uint32_t height)
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

    void Renderer::BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex)
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

    void Renderer::PollShaders()
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
        {
            auto info = rc->m_passInfo;

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
