#include "Render/SceneRenderTargets.h"
#include "API/Command.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Sampler.h"
#include "API/Swapchain.h"

#include <algorithm>
#include <cmath>

namespace pe
{
    namespace
    {
        float GetRenderTargetScale(bool useRenderTargetScale)
        {
            const auto &gSettings = Settings::Get<SceneSettings>();
            return useRenderTargetScale ? gSettings.render_scale : 1.f;
        }

        uint32_t GetScaledRenderWidth(bool useRenderTargetScale)
        {
            return static_cast<uint32_t>(RHII.GetWidthf() * GetRenderTargetScale(useRenderTargetScale));
        }

        uint32_t GetScaledRenderHeight(bool useRenderTargetScale)
        {
            return static_cast<uint32_t>(RHII.GetHeightf() * GetRenderTargetScale(useRenderTargetScale));
        }

    } // namespace

    void DestroySceneRenderTargets(SceneRenderTargetMap &renderTargets, SceneRenderTargetMap &depthStencilTargets)
    {
        for (auto &rt : renderTargets)
            Image::Destroy(rt.second);
        renderTargets.clear();

        for (auto &rt : depthStencilTargets)
            Image::Destroy(rt.second);
        depthStencilTargets.clear();
    }

    Image *CreateSceneRenderTarget(SceneRenderTargetMap &renderTargets,
                                   const std::string &name,
                                   ::PeFormat format,
                                   PeImageUsageFlags usage,
                                   bool useRenderTargetScale,
                                   bool useMips,
                                   vec4 clearColor)
    {
        if (Image *existing = GetSceneRenderTarget(renderTargets, name))
            return existing;

        auto &gSettings = Settings::Get<SceneSettings>();

        const uint32_t width = GetScaledRenderWidth(useRenderTargetScale);
        const uint32_t height = GetScaledRenderHeight(useRenderTargetScale);

        ImageDesc desc{};
        desc.format = format;
        desc.width = width;
        desc.height = height;
        desc.usage = usage | PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE |
                     PE_IMAGE_USAGE_TRANSFER_DST;
        if (useMips)
            desc.mipLevels = static_cast<uint32_t>(std::floor(std::log2(width > height ? width : height))) + 1;
        desc.clearColor = clearColor;
        desc.name = name;
        Image *rt = Image::Create(desc);
        rt->SetClearColor(clearColor);

        rt->CreateRTV();
        rt->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
        rt->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.anisotropyEnable = false;
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        rt->SetSampler(sampler);

        gSettings.rendering_images.push_back(rt);
        renderTargets[StringHash(name)] = rt;

        return rt;
    }

    Image *CreateSceneDepthStencilTarget(SceneRenderTargetMap &depthStencilTargets,
                                         const std::string &name,
                                         ::PeFormat format,
                                         PeImageUsageFlags usage,
                                         bool useRenderTargetScale,
                                         float clearDepth,
                                         uint32_t clearStencil)
    {
        if (Image *existing = GetSceneRenderTarget(depthStencilTargets, name))
            return existing;

        auto &gSettings = Settings::Get<SceneSettings>();

        ImageDesc desc{};
        desc.width = GetScaledRenderWidth(useRenderTargetScale);
        desc.height = GetScaledRenderHeight(useRenderTargetScale);
        desc.usage = usage | PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED |
                     PE_IMAGE_USAGE_TRANSFER_DST;
        desc.format = format;
        desc.clearColor = vec4(clearDepth, static_cast<float>(clearStencil), 0.f, 0.f);
        desc.name = name;
        Image *depth = Image::Create(desc);
        depth->SetClearColor(desc.clearColor);

        depth->CreateRTV();
        depth->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = false;
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        depth->SetSampler(sampler);

        gSettings.rendering_images.push_back(depth);
        depthStencilTargets[StringHash(name)] = depth;

        return depth;
    }

    Image *GetSceneRenderTarget(const SceneRenderTargetMap &renderTargets, const std::string &name)
    {
        return GetSceneRenderTarget(renderTargets, StringHash(name));
    }

    Image *GetSceneRenderTarget(const SceneRenderTargetMap &renderTargets, size_t hash)
    {
        auto it = renderTargets.find(hash);
        return it != renderTargets.end() ? it->second : nullptr;
    }

    bool DestroySceneRenderTarget(SceneRenderTargetMap &renderTargets, const std::string &name)
    {
        auto it = renderTargets.find(StringHash(name));
        if (it == renderTargets.end())
            return false;

        Image *image = it->second;
        auto &renderingImages = Settings::Get<SceneSettings>().rendering_images;
        renderingImages.erase(std::remove(renderingImages.begin(), renderingImages.end(), image), renderingImages.end());
        CommandBuffer::ClearFramebufferCache();
        Image::Destroy(image);
        renderTargets.erase(it);
        return true;
    }

    Image *CreateSceneFSSampledImage(const std::string &name, bool useRenderTargetScale)
    {
        ImageDesc desc{};
        desc.format = RHII.GetSwapchainFormat();
        desc.width = GetScaledRenderWidth(useRenderTargetScale);
        desc.height = GetScaledRenderHeight(useRenderTargetScale);
        desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
        desc.name = name;
        Image *sampledImage = Image::Create(desc);

        sampledImage->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        sampledImage->SetSampler(sampler);

        return sampledImage;
    }

    SceneRenderTargets CreateDefaultSceneRenderTargets(SceneRenderTargetMap &renderTargets,
                                                       SceneRenderTargetMap &depthStencilTargets)
    {
        for (auto &framebuffer : CommandBuffer::GetFramebuffers())
            Framebuffer::Destroy(framebuffer.second);
        CommandBuffer::GetFramebuffers().clear();

        DestroySceneRenderTargets(renderTargets, depthStencilTargets);
        Settings::Get<SceneSettings>().rendering_images.clear();

        const ::PeFormat surfaceFormat = RHII.GetSwapchainFormat();

        SceneRenderTargets targets{};
        targets.depthStencil =
            CreateSceneDepthStencilTarget(depthStencilTargets, "depthStencil", RHII.GetDepthFormat(), PE_IMAGE_USAGE_TRANSFER_DST);
        targets.viewport =
            CreateSceneRenderTarget(renderTargets, "viewport", surfaceFormat, PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST);
        targets.display = CreateSceneRenderTarget(renderTargets,
                                                  "display",
                                                  surfaceFormat,
                                                  PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST,
                                                  false);
        targets.screenshot = CreateSceneRenderTarget(renderTargets,
                                                     "screenshot",
                                                     surfaceFormat,
                                                     PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST,
                                                     false);
        CreateSceneRenderTarget(renderTargets, "normal", PE_FORMAT_R16G16B16A16_SFLOAT);
        CreateSceneRenderTarget(renderTargets, "albedo", surfaceFormat);
        CreateSceneRenderTarget(renderTargets, "srm", surfaceFormat);
        CreateSceneRenderTarget(renderTargets, "velocity", PE_FORMAT_R16G16_SFLOAT);
        CreateSceneRenderTarget(renderTargets, "emissive", surfaceFormat);
        CreateSceneRenderTarget(renderTargets, "transparency", PE_FORMAT_R8_UNORM, PE_IMAGE_USAGE_NONE, true, false, Color::Black);

        return targets;
    }

    void BlitSceneImageToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex)
    {
        Image *swapchainImage = RHII.GetSwapchain()->GetImage(imageIndex);

        ImageBlit region{};
        region.srcOffsets[1] = {static_cast<int32_t>(src->GetWidth()), static_cast<int32_t>(src->GetHeight()), 1};
        region.srcSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[1] = {static_cast<int32_t>(swapchainImage->GetWidth()), static_cast<int32_t>(swapchainImage->GetHeight()), 1};
        region.dstSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.dstSubresource.layerCount = 1;

        ImageBarrierInfo barrier{};
        barrier.image = swapchainImage;
        barrier.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
        barrier.stageFlags = PE_STAGE_ALL_COMMANDS;
        barrier.accessMask = PE_ACCESS_NONE;

        const PeFilter filter = src->GetWidth() == swapchainImage->GetWidth() && src->GetHeight() == swapchainImage->GetHeight()
                                    ? PE_FILTER_NEAREST
                                    : PE_FILTER_LINEAR;
        cmd->BlitImage(src, swapchainImage, region, filter);
        cmd->ImageBarrier(barrier);
    }
} // namespace pe
