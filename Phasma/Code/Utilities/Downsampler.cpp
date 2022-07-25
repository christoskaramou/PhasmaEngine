#include "Utilities/Downsampler.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Queue.h"

namespace pe
{
    Downsampler::Downsampler()
    {
        m_pipeline = nullptr;
        m_pipelineInfo = {};
        m_DSet = {};
        m_image = nullptr;
        m_image6 = nullptr;
        memset(m_counter, 0, 6 * sizeof(uint32_t));
        m_pushConstants = {};
    }

    Downsampler::~Downsampler()
    {
    }

    void Downsampler::Init()
    {
    }

    void Downsampler::UpdatePipelineInfo()
    {
        // SPD defines
        const std::vector<Define> defines{
            Define{"A_GPU", ""},
            Define{"A_GLSL", ""},
            Define{"SPD_LINEAR_SAMPLER", ""},
            Define{"SPD_NO_WAVE_OPERATIONS", ""}};

        m_pipelineInfo = std::make_shared<PipelineCreateInfo>();
        m_pipelineInfo->pCompShader = Shader::Create(ShaderInfo{"Shaders/Compute/spd/spd.comp", ShaderStage::ComputeBit, defines});
        m_pipelineInfo->descriptorSetLayouts = {m_DSet->GetLayout()};
        m_pipelineInfo->pushConstantSize = sizeof(PushConstants);
        m_pipelineInfo->pushConstantStage = ShaderStage::ComputeBit;
        m_pipelineInfo->name = "Downsample_pipeline";
    }

    void Downsampler::CreateUniforms(CommandBuffer *cmd)
    {
        m_atomicCounter = Buffer::Create(
            sizeof(m_counter),
            BufferUsage::StorageBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Downsample_storage_buffer");

        std::vector<DescriptorBindingInfo> bindingInfos(5);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::StorageImage;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::StorageImage;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::StorageBuffer;

        m_DSet = Descriptor::Create(bindingInfos, ShaderStage::ComputeBit, "Downsample_descriptor");

        UpdateDescriptorSets();
    }

    void Downsampler::UpdateDescriptorSets()
    {
        m_DSet->SetImage(0, m_image); // TODO: figure how to set different image views, maybe change to view inputs?
        m_DSet->SetImage(1, m_image6);
        m_DSet->SetBuffer(2, m_atomicCounter);
        m_DSet->UpdateDescriptor();
    }

    void Downsampler::Update(Camera *camera)
    {
    }

    uvec2 Downsampler::SpdSetup()
    {
        const Rect2Du rectInfo{0, 0, m_image->imageInfo.width, m_image->imageInfo.height};

        m_pushConstants.workGroupOffset.x = rectInfo.x / 64;
        m_pushConstants.workGroupOffset.y = rectInfo.y / 64;

        const uint32_t endIndexX = (rectInfo.x + rectInfo.width - 1) / 64;
        const uint32_t endIndexY = (rectInfo.y + rectInfo.height - 1) / 64;

        uvec2 dispatchThreadGroupCount(endIndexX + 1 - m_pushConstants.workGroupOffset.x,
                                       endIndexY + 1 - m_pushConstants.workGroupOffset.y);

        m_pushConstants.numWorkGroupsPerSlice = dispatchThreadGroupCount.x * dispatchThreadGroupCount.y;
        m_pushConstants.mips = m_image->imageInfo.mipLevels;

        return dispatchThreadGroupCount;
    }

    void Downsampler::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        uvec2 dispatchThreadGroupCount = SpdSetup();

        Queue *queue = RHII.GetComputeQueue();
        queue->BeginDebugRegion("Downsampler::Dispatch Queue");

        cmd->Begin();
        cmd->BeginDebugRegion("Compute::Dispatch command");
        cmd->BindPipeline(*m_pipelineInfo, &m_pipeline);
        cmd->BindComputeDescriptors(m_pipeline, 1, &m_DSet);
        cmd->PushConstants(m_pipeline, ShaderStage::ComputeBit, 0, sizeof(PushConstants), &m_pushConstants);
        cmd->Dispatch(dispatchThreadGroupCount.x, dispatchThreadGroupCount.y, m_image->imageInfo.arrayLayers);
        cmd->EndDebugRegion();
        cmd->End();

        PipelineStageFlags waitStage{PipelineStage::ComputeShaderBit};
        cmd->Submit(queue, &waitStage, 0, nullptr, 0, nullptr);

        queue->EndDebugRegion();
    }

    void Downsampler::Resize(uint32_t width, uint32_t height)
    {
    }

    void Downsampler::Destroy()
    {
    }
}
