#include "SharpenPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Systems/RendererSystem.h"
#include "TAAPass.h"

namespace pe
{
    SharpenPass::SharpenPass()
    {
    }

    SharpenPass::~SharpenPass()
    {
    }

    void SharpenPass::Init()
    {
    }

    void SharpenPass::UpdatePassInfo()
    {
        m_passInfo->name = "Sharpen_Pipeline";
        m_passInfo->pCompShader = Shader::Create(Path::Assets + "Shaders/Sharpen/RCAS.hlsl", vk::ShaderStageFlagBits::eCompute, "main", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->Update();
    }

    void SharpenPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void SharpenPass::UpdateDescriptorSets()
    {
        TAAPass *taa = GetGlobalComponent<TAAPass>();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        Image *taaResolved = taa->GetResolvedImage();
        Image *displayRT = rs->GetDisplayRT();

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);
            if (!descriptors.empty())
            {
                Descriptor *dset = descriptors[0];
                dset->SetImageView(0, taaResolved->GetSRV(), nullptr);
                dset->SetImageView(1, displayRT->GetUAV(0), nullptr);
                dset->Update();
            }
        }
    }

    void SharpenPass::Update()
    {
    }

    void SharpenPass::Destroy()
    {
    }

    void SharpenPass::Resize(uint32_t width, uint32_t height)
    {
        UpdateDescriptorSets();
    }

    void SharpenPass::ExecutePass(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();

        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        Image *output = rs->GetDisplayRT();
        TAAPass *taa = GetGlobalComponent<TAAPass>();
        Image *input = taa->GetResolvedImage();

        struct PushConstants
        {
            float sharpness;
        };
        PushConstants pc{};
        pc.sharpness = gSettings.cas_sharpness;

        std::vector<ImageBarrierInfo> barriers;
        barriers.push_back({input, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead});
        barriers.push_back({output, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite});

        cmd->BeginDebugRegion("RCAS");
        cmd->ImageBarriers(barriers);
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        uint32_t groupX = (output->GetWidth() + 7) / 8;
        uint32_t groupY = (output->GetHeight() + 7) / 8;
        cmd->Dispatch(groupX, groupY, 1);
        cmd->EndDebugRegion();
    }
} // namespace pe
