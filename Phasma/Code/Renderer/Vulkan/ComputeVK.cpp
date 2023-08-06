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
#include "Renderer/Pipeline.h"

namespace pe
{
    Compute::Compute()
    {
        SBIn = nullptr;
        SBOut = nullptr;
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
        commandBuffer->BindPipeline(*passInfo);
        commandBuffer->BindDescriptors(1, &DSet);
        commandBuffer->Dispatch(sizeX, sizeY, sizeZ);
        commandBuffer->EndDebugRegion();
        commandBuffer->End();

        commandBuffer->Submit(RHII.GetComputeQueue(), 0, nullptr, nullptr, 0, nullptr, nullptr);

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
        DSet->Update();
    }

    void Compute::UpdatePassInfo(const std::string &shaderName)
    {
        passInfo = std::make_shared<PassInfo>();
        passInfo->pCompShader = Shader::Create(shaderName, ShaderStage::ComputeBit);
        passInfo->descriptorSetLayouts = {DSet->GetLayout()};
        passInfo->name = "Compute_pipeline";
    }

    void Compute::Destroy()
    {
        CommandBuffer::Return(commandBuffer);

        Buffer::Destroy(SBIn);
        SBIn = nullptr;
        Buffer::Destroy(SBOut);
        SBOut = nullptr;

        Descriptor::Destroy(DSet);
        DSet = nullptr;
    }

    Compute Compute::Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, const std::string &name)
    {
        Compute compute;
        compute.commandBuffer = CommandBuffer::GetFree(RHII.GetComputeQueue()->GetFamilyId());
        compute.CreateUniforms(sizeIn, sizeOut);
        compute.UpdatePassInfo(shaderName);

        return compute;
    }

    std::vector<Compute> Compute::Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, uint32_t count, const std::string &name)
    {
        std::vector<CommandBuffer *> commandBuffers(count);
        std::vector<Compute> computes(count);

        for (uint32_t i = 0; i < count; i++)
        {
            Compute compute;
            compute.commandBuffer = CommandBuffer::GetFree(RHII.GetComputeQueue()->GetFamilyId());
            compute.CreateUniforms(sizeIn, sizeOut);
            compute.UpdatePassInfo(shaderName);

            computes.push_back(compute);
        }

        return computes;
    }

    void Compute::DestroyResources()
    {
    }
}
#endif
