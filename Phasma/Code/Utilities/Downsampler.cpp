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
        m_input = nullptr;
        m_output = nullptr;
        m_output5 = nullptr;
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
        m_input = nullptr;
        m_output = nullptr;
        m_output5 = nullptr;
        m_buffer = Buffer::Create(
            sizeof(m_counter),
            BufferUsage::StorageBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Downsample_storage_buffer");

        std::vector<DescriptorBindingInfo> bindingInfos(1);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::StorageBuffer;
        m_DSet = Descriptor::Create(bindingInfos, ShaderStage::ComputeBit, "Downsample_descriptor");

        UpdateDescriptorSets();
    }

    void Downsampler::UpdateDescriptorSets()
    {
        m_DSet->SetBuffer(0, m_buffer);
        m_DSet->UpdateDescriptor();
    }

    void Downsampler::Update(Camera *camera)
    {
    }

    void Downsampler::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        Queue *queue = RHII.GetComputeQueue(imageIndex);
        queue->BeginDebugRegion("Downsampler::Dispatch Queue");

        cmd->Begin();
        cmd->BeginDebugRegion("Compute::Dispatch command");
        cmd->BindPipeline(*m_pipelineInfo, &m_pipeline);
        cmd->BindComputeDescriptors(m_pipeline, 1, &m_DSet);
        // cmd->Dispatch(sizeX, sizeY, sizeZ);
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
