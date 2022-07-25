#if PE_VULKAN
#include "Renderer/Compute.h"
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
    Compute::Compute()
    {
        SBIn = nullptr;
        SBOut = nullptr;
        pipeline = nullptr;
        DSet = nullptr;
        commandBuffer = nullptr;
    }

    void Compute::UpdateInput(void *data, size_t size, size_t offset)
    {
        MemoryRange mr{};
        mr.data = data;
        mr.size = size;
        mr.offset = offset;
        SBIn->Copy(1, &mr, false);
    }

    void Compute::Dispatch(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
    {
        RHII.GetComputeQueue()->BeginDebugRegion("Compute::Dispatch queue");
        
        commandBuffer->Begin();
        commandBuffer->BeginDebugRegion("Compute::Dispatch command");
        commandBuffer->BindComputePipeline(pipeline);
        commandBuffer->BindComputeDescriptors(pipeline, 1, &DSet);
        commandBuffer->Dispatch(sizeX, sizeY, sizeZ);
        commandBuffer->EndDebugRegion();
        commandBuffer->End();
        
        commandBuffer->Submit(RHII.GetComputeQueue(), nullptr, 0, nullptr, 0, nullptr);

        RHII.GetComputeQueue()->EndDebugRegion();
    }

    void Compute::Wait()
    {
        commandBuffer->Wait();
    }

    void Compute::CreateUniforms(size_t sizeIn, size_t sizeOut)
    {
        SBIn = Buffer::Create(
            sizeIn,
            BufferUsage::StorageBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Compute_Input_storage_buffer");

        SBOut = Buffer::Create(
            sizeOut,
            BufferUsage::StorageBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Compute_Output_storage_buffer");

        std::vector<DescriptorBindingInfo> bindingInfos(2);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::StorageBuffer;
        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::StorageBuffer;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::ComputeBit, "Compute_descriptor");

        UpdateDescriptorSet();
    }

    void Compute::UpdateDescriptorSet()
    {
        DSet->SetBuffer(0, SBIn);
        DSet->SetBuffer(1, SBOut);
        DSet->UpdateDescriptor();
    }

    void Compute::CreatePipeline(const std::string &shaderName)
    {
        if (pipeline)
            Pipeline::Destroy(pipeline);

        PipelineCreateInfo info{};
        info.pCompShader = Shader::Create(ShaderInfo{shaderName, ShaderStage::ComputeBit});
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.name = "Compute_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pCompShader);
    }

    void Compute::Destroy()
    {
        CommandBuffer::Return(commandBuffer);

        Buffer::Destroy(SBIn);
        Buffer::Destroy(SBOut);

        Pipeline::Destroy(pipeline);

        Descriptor::Destroy(DSet);
    }

    Compute Compute::Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, const std::string &name)
    {
        Compute compute;
        compute.commandBuffer = CommandBuffer::GetNext(RHII.GetComputeQueue()->GetFamilyId());
        compute.CreateUniforms(sizeIn, sizeOut);
        compute.CreatePipeline(shaderName);

        return compute;
    }

    std::vector<Compute> Compute::Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, uint32_t count, const std::string &name)
    {
        std::vector<CommandBuffer *> commandBuffers(count);
        std::vector<Compute> computes(count);

        for (uint32_t i = 0; i < count; i++)
        {
            Compute compute;
            compute.commandBuffer = CommandBuffer::GetNext(RHII.GetComputeQueue()->GetFamilyId());
            compute.CreateUniforms(sizeIn, sizeOut);
            compute.CreatePipeline(shaderName);

            computes.push_back(compute);
        }

        return computes;
    }

    void Compute::DestroyResources()
    {
    }
}
#endif
