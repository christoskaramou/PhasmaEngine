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
            info.format = m_displayRT->GetFormat();
            info.extent = vk::Extent3D{m_displayRT->GetWidth(), m_displayRT->GetHeight(), 1};
            info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eColorAttachment;
            m_historyImage = Image::Create(info, "TAA_History");
            m_historyImage->CreateRTV();
            m_historyImage->CreateSRV(vk::ImageViewType::e2D);

            // Linear sampler for history
            vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            Sampler *sampler = Sampler::Create(samplerInfo, "TAA_History_Linear");
            m_historyImage->SetSampler(sampler);
        }

        if (!m_taaResolved)
        {
            vk::ImageCreateInfo info = Image::CreateInfoInit();
            info.format = m_displayRT->GetFormat();
            info.extent = vk::Extent3D{m_displayRT->GetWidth(), m_displayRT->GetHeight(), 1};
            info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment;
            m_taaResolved = Image::Create(info, "TAA_Resolved");
            m_taaResolved->CreateRTV();
            m_taaResolved->CreateSRV(vk::ImageViewType::e2D);
            m_taaResolved->CreateUAV(vk::ImageViewType::e2D, 0);
        }

        m_jitterIndex = 0;
        m_jitterPhaseCount = 16;
        m_casSharpeningEnabled = Settings::Get<GlobalSettings>().cas_sharpening;
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
        auto &gSettings = Settings::Get<GlobalSettings>();
        Image *taaOutput = gSettings.cas_sharpening ? m_taaResolved : m_displayRT;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);
            if (!descriptors.empty())
            {
                Descriptor *dset = descriptors[0];
                dset->SetImageView(0, m_viewportRT->GetSRV());
                dset->SetImageView(1, m_historyImage->GetSRV());
                dset->SetImageView(2, m_velocityRT->GetSRV());
                // dset->SetImageView(3, m_depthStencil->GetSRV()); // Depth unused
                dset->SetSampler(4, m_historyImage->GetSampler());
                dset->SetImageView(5, taaOutput->GetUAV(0));
                dset->Update();
            }
        }
    }

    void TAAPass::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (gSettings.cas_sharpening != m_casSharpeningEnabled)
        {
            m_casSharpeningEnabled = gSettings.cas_sharpening;
            UpdateDescriptorSets();
        }
    }

    void TAAPass::ExecutePass(CommandBuffer *cmd)
    {

        Image *taaOutput = m_casSharpeningEnabled ? m_taaResolved : m_displayRT;

        struct TAAConstants
        {
            vec2 resolution;  // Input resolution
            vec2 displaySize; // Output resolution
            vec2 jitter;
            vec2 padding;
        };

        TAAConstants pc{};
        pc.resolution = vec2(m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        pc.displaySize = vec2(taaOutput->GetWidth_f(), taaOutput->GetHeight_f());
        pc.jitter = m_jitter;

        // Barriers
        std::vector<ImageBarrierInfo> barriers;
        barriers.push_back({m_viewportRT, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({m_historyImage, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({m_velocityRT, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        // barriers.push_back({m_depthStencil, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({taaOutput, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite});

        cmd->BeginDebugRegion("TAA");
        cmd->ImageBarriers(barriers);

        // Bind Pipeline & Descriptors
        // Note: BindPipeline auto-binds descriptors if set true (default).
        cmd->BindPipeline(*m_passInfo);

        cmd->SetConstants(pc);
        cmd->PushConstants();

        // Dispatch
        uint32_t groupX = (taaOutput->GetWidth() + 7) / 8;
        uint32_t groupY = (taaOutput->GetHeight() + 7) / 8;
        cmd->Dispatch(groupX, groupY, 1);

        // Copy Display -> History
        ImageBarrierInfo copyBarrierSrc{};
        copyBarrierSrc.image = taaOutput;
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

        // Copy Resolved -> History
        cmd->CopyImage(taaOutput, m_historyImage);
        cmd->EndDebugRegion();
    }

    void TAAPass::Resize(uint32_t width, uint32_t height)
    {
        Destroy();
        Init();
        UpdateDescriptorSets();
    }

    void TAAPass::Destroy()
    {
        Image::Destroy(m_historyImage);
        Image::Destroy(m_taaResolved);
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
