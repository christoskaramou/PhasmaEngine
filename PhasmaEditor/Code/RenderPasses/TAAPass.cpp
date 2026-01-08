#include "TAAPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    float Halton(int index, int base)
    {
        float f = 1;
        float r = 0;
        int current = index;
        while (current > 0)
        {
            f = f / base;
            r = r + f * (current % base);
            current = floor(current / base);
        }
        return r;
    }

    void TAAPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_displayRT = rs->GetDisplayRT();
        m_viewportRT = rs->GetViewportRT(); // Represents current input color
        m_depthStencil = rs->GetDepthStencilRT();
        m_velocityRT = rs->GetRenderTarget("velocity");

        // Create history image if not exists
        if (!m_historyImage)
        {
            vk::ImageCreateInfo info = Image::CreateInfoInit();
            info.format = m_viewportRT->GetFormat();
            info.extent = vk::Extent3D{m_viewportRT->GetWidth(), m_viewportRT->GetHeight(), 1};
            info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eColorAttachment;
            m_historyImage = Image::Create(info, "TAA_History");
            m_historyImage->CreateSRV(vk::ImageViewType::e2D);
            m_historyImage->CreateRTV();

            // Linear sampler for history
            vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            Sampler *sampler = Sampler::Create(samplerInfo, "TAA_History_Linear");
            m_historyImage->SetSampler(sampler);
        }

        m_jitterIndex = 0;
        m_jitterPhaseCount = 16;
    }

    void TAAPass::UpdatePassInfo()
    {
        m_passInfo->name = "TAA_pipeline";
        m_passInfo->pCompShader = Shader::Create(Path::Assets + "Shaders/TAA/TAA.hlsl", vk::ShaderStageFlagBits::eCompute, "main", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->Update();
    }

    void TAAPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void TAAPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);
            if (!descriptors.empty())
            {
                Descriptor *dset = descriptors[0];
                dset->SetImageView(0, m_viewportRT->GetSRV(), m_viewportRT->GetSampler()->ApiHandle());
                dset->SetImageView(1, m_historyImage->GetSRV(), m_historyImage->GetSampler()->ApiHandle());
                dset->SetImageView(2, m_velocityRT->GetSRV(), m_velocityRT->GetSampler()->ApiHandle());
                dset->SetImageView(3, m_depthStencil->GetSRV(), m_depthStencil->GetSampler()->ApiHandle());
                // Sampler at binding 4? No, sampler is likely immutable or separate?
                // Shader:
                // binding 0: Texture2D in_color
                // binding 1: Texture2D in_history
                // binding 2: Texture2D in_velocity
                // binding 3: Texture2D in_depth
                // binding 4: SamplerState sampler_linear_clamp
                // binding 5: RWTexture2D out_color

                // Usually samplers are combined with images or separate. The shader has `register(s0)` for sampler.
                // If using HLSL, default reflection might map valid bindings.
                // Binding 4 is Sampler.
                dset->SetSampler(4, m_historyImage->GetSampler()->ApiHandle());

                // Binding 5: Storage Image
                dset->SetImageView(5, m_displayRT->GetSRV(), nullptr); // or dedicated UAV view if required. eStorage usage implies SRV/UAV.

                dset->Update();
            }
        }
    }

    void TAAPass::Update()
    {
    }

    void TAAPass::Draw(CommandBuffer *cmd)
    {
        struct TAAConstants
        {
            vec2 resolution;
            vec2 jitter;
        };

        TAAConstants pc{};
        pc.resolution = vec2(m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        pc.jitter = m_jitter;

        // Barriers
        std::vector<ImageBarrierInfo> barriers;
        barriers.push_back({m_viewportRT, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({m_historyImage, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({m_velocityRT, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({m_depthStencil, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({m_displayRT, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite});

        cmd->ImageBarriers(barriers);

        // Bind Pipeline & Descriptors
        // Note: BindPipeline auto-binds descriptors if set true (default).
        cmd->BindPipeline(*m_passInfo);

        cmd->SetConstants(pc);
        cmd->PushConstants();

        // Dispatch
        uint32_t groupX = (m_displayRT->GetWidth() + 7) / 8;
        uint32_t groupY = (m_displayRT->GetHeight() + 7) / 8;

        cmd->Dispatch(groupX, groupY, 1);

        // Copy Display -> History
        ImageBarrierInfo copyBarrierSrc{};
        copyBarrierSrc.image = m_displayRT;
        copyBarrierSrc.layout = vk::ImageLayout::eTransferSrcOptimal;
        copyBarrierSrc.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        copyBarrierSrc.accessMask = vk::AccessFlagBits2::eTransferRead;

        ImageBarrierInfo copyBarrierDst{};
        copyBarrierDst.image = m_historyImage;
        copyBarrierDst.layout = vk::ImageLayout::eTransferDstOptimal;
        copyBarrierDst.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        copyBarrierDst.accessMask = vk::AccessFlagBits2::eTransferWrite;

        cmd->ImageBarrier(copyBarrierSrc);
        cmd->ImageBarrier(copyBarrierDst);

        cmd->CopyImage(m_displayRT, m_historyImage);
    }
    // Transition Display back to attachment optimal? or leave as transfer src?
    // RendererSystem might expect something else. Renderer usually transitions to ePresent or similar later.
    // Actually RendererSystem::RecordPasses adds barriers too if needed or BlitToSwapchain does.

    void TAAPass::Resize(uint32_t width, uint32_t height)
    {
        if (m_historyImage)
        {
            Image::Destroy(m_historyImage);
            m_historyImage = nullptr;
        }
        Init();
        UpdateDescriptorSets();
    }

    void TAAPass::Destroy()
    {
        Image::Destroy(m_historyImage);
    }

    void TAAPass::GenerateJitter()
    {
        m_jitterIndex = (m_jitterIndex + 1) % m_jitterPhaseCount;

        // Halton sequence (2, 3)
        // -0.5 to 0.5 range
        float x = Halton(m_jitterIndex + 1, 2) - 0.5f;
        float y = Halton(m_jitterIndex + 1, 3) - 0.5f;

        m_jitter = vec2(x, y);

        // Projection jitter needs to be in clip space (-1 to 1), scaled by pixel size
        // ProjMatrix offset = (2 * jitterX / width, 2 * jitterY / height)
        // Usually jitter is in pixels, e.g. [-0.5, 0.5]

        float width = m_viewportRT->GetWidth_f();
        float height = m_viewportRT->GetHeight_f();

        m_projectionJitter.x = (2.0f * m_jitter.x) / width;
        m_projectionJitter.y = (2.0f * m_jitter.y) / height;
    }
} // namespace pe
