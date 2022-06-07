/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#if PE_VULKAN
#include "Renderer/Compute.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Fence.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Queue.h"

namespace pe
{
    Compute::Compute()
    {
        fence = {};
        semaphore = {};
        DSCompute = {};
        commandBuffer = {};
        pipeline = nullptr;
    }

    void Compute::UpdateInput(const void *srcData, size_t srcSize, size_t offset)
    {
        SBIn->Map();
        SBIn->CopyData(srcData, srcSize, offset);
        SBIn->Flush();
        SBIn->Unmap();
    }

    void Compute::Dispatch(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
    {
        Debug::BeginQueueRegion(s_queue, "Compute::Dispatch queue");
        commandBuffer->Begin();
        Debug::BeginCmdRegion(commandBuffer->Handle(), "Compute::Dispatch command");
        commandBuffer->BindComputePipeline(pipeline);
        commandBuffer->BindComputeDescriptors(pipeline, 1, &DSCompute);
        commandBuffer->Dispatch(sizeX, sizeY, sizeZ);
        Debug::EndCmdRegion(commandBuffer->Handle());
        commandBuffer->End();

        static PipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
        s_queue->Submit(1, &commandBuffer,
                        waitStages,
                        0, nullptr,
                        0, nullptr,
                        fence);

        Debug::EndQueueRegion(s_queue);
    }

    void Compute::WaitFence()
    {
        fence->Wait();
        fence->Reset();
    }

    void Compute::CreateComputeStorageBuffers(size_t sizeIn, size_t sizeOut)
    {
        SBIn = Buffer::Create(
            sizeIn,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Compute_Input_storage_buffer");
        SBIn->Map();
        SBIn->Zero();
        SBIn->Flush();
        SBIn->Unmap();

        SBOut = Buffer::Create(
            sizeOut,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Compute_Output_storage_buffer");
        SBOut->Map();
        SBOut->Zero();
        SBOut->Flush();
        SBOut->Unmap();
    }

    void Compute::CreateDescriptorSet()
    {
        DSCompute = Descriptor::Create(Pipeline::getDescriptorSetLayoutCompute(), 1, "Compute_descriptor");
    }

    void Compute::UpdateDescriptorSet()
    {
        std::array<DescriptorUpdateInfo, 2> infos{};

        infos[0].binding = 0;
        infos[0].pBuffer = SBIn;
        infos[0].bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        infos[1].binding = 1;
        infos[1].pBuffer = SBOut;
        infos[1].bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        DSCompute->UpdateDescriptor(2, infos.data());
    }

    void Compute::CreatePipeline(const std::string &shaderName)
    {
        if (pipeline)
            Pipeline::Destroy(pipeline);

        PipelineCreateInfo info{};
        info.pCompShader = Shader::Create(ShaderInfo{shaderName, ShaderType::Compute});
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutCompute()};
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
        Semaphore::Destroy(semaphore);
        Fence::Destroy(fence);
    }

    Compute Compute::Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, const std::string &name)
    {
        CreateResources();

        Compute compute;
        compute.commandBuffer = CommandBuffer::GetNext(s_queue->GetFamilyId());
        compute.CreatePipeline(shaderName);
        compute.CreateDescriptorSet();
        compute.CreateComputeStorageBuffers(sizeIn, sizeOut);
        compute.UpdateDescriptorSet();
        compute.fence = Fence::GetNext();
        compute.semaphore = Semaphore::Create(false, name + "_semaphore");

        return compute;
    }

    std::vector<Compute> Compute::Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, uint32_t count, const std::string &name)
    {
        CreateResources();

        std::vector<CommandBuffer *> commandBuffers(count);
        std::vector<Compute> computes(count);

        for (uint32_t i = 0; i < count; i++)
        {
            Compute compute;
            compute.commandBuffer = CommandBuffer::GetNext(s_queue->GetFamilyId());
            compute.CreatePipeline(shaderName);
            compute.CreateDescriptorSet();
            compute.CreateComputeStorageBuffers(sizeIn, sizeOut);
            compute.UpdateDescriptorSet();
            compute.fence = Fence::GetNext();
            compute.semaphore = Semaphore::Create(false, name + "_semaphore_" + std::to_string(i));

            computes.push_back(compute);
        }

        return computes;
    }

    void Compute::CreateResources()
    {
        if (!s_queue)
            s_queue = Queue::GetNext(QueueType::ComputeBit, 1);
    }

    void Compute::DestroyResources()
    {
        DescriptorLayout::Destroy(Pipeline::getDescriptorSetLayoutCompute());
    }
}
#endif
