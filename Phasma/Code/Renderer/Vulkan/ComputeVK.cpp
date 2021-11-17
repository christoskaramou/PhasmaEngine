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
#include "Renderer/Fence.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"

namespace pe
{
    CommandPool* Compute::s_commandPool = nullptr;

    Compute::Compute()
    {
        fence = {};
        semaphore = {};
        DSCompute = {};
        commandBuffer = {};
        pipeline = nullptr;
    }

    void Compute::updateInput(const void* srcData, size_t srcSize, size_t offset)
    {
        SBIn->Map();
        SBIn->CopyData(srcData, srcSize, offset);
        SBIn->Flush();
        SBIn->Unmap();
    }

    void Compute::dispatch(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ, uint32_t count, Semaphore** waitForHandles)
    {
        commandBuffer->Begin();
        commandBuffer->BindComputePipeline(pipeline);
        commandBuffer->BindComputeDescriptors(pipeline, 1, &DSCompute);
        commandBuffer->Dispatch(sizeX, sizeY, sizeZ);
        commandBuffer->End();

        std::vector<VkSemaphore> waitSemaphores(count);
        for (size_t i = 0; i < count; i++)
            waitSemaphores[i] = waitForHandles[i]->Handle();

        VkCommandBuffer cmdBuffer = commandBuffer->Handle();
        VkSemaphore vksemaphore = semaphore->Handle();
        VkSubmitInfo siCompute{};
        siCompute.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        siCompute.commandBufferCount = 1;
        siCompute.pCommandBuffers = &cmdBuffer;
        siCompute.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
        siCompute.pWaitSemaphores = waitSemaphores.data();
        siCompute.pSignalSemaphores = &vksemaphore;

        vkQueueSubmit(RHII.computeQueue, 1, &siCompute, fence->Handle());
    }

    void Compute::waitFence()
    {
        RHII.WaitFence(fence);
    }

    void Compute::createComputeStorageBuffers(size_t sizeIn, size_t sizeOut)
    {
        SBIn = Buffer::Create(
            sizeIn,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        SBIn->Map();
        SBIn->Zero();
        SBIn->Flush();
        SBIn->Unmap();

        SBOut = Buffer::Create(
            sizeOut,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        SBOut->Map();
        SBOut->Zero();
        SBOut->Flush();
        SBOut->Unmap();
    }

    void Compute::createDescriptorSet()
    {
        DSCompute = Descriptor::Create(Pipeline::getDescriptorSetLayoutCompute());
    }

    void Compute::updateDescriptorSet()
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

    void Compute::createPipeline(const std::string& shaderName)
    {
        if (pipeline)
            pipeline->Destroy();

        Shader shader {shaderName, ShaderType::Compute};

        PipelineCreateInfo info{};
        info.pCompShader = &shader;
        info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutCompute() };

        pipeline = Pipeline::Create(info);
    }

    void Compute::destroy()
    {
        SBIn->Destroy();
        SBOut->Destroy();
        pipeline->Destroy();
        semaphore->Destroy();
        fence->Destroy();
    }

    Compute Compute::Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut)
    {
        CreateResources();

        Compute compute;
        compute.commandBuffer = CommandBuffer::Create(s_commandPool);
        compute.createPipeline(shaderName);
        compute.createDescriptorSet();
        compute.createComputeStorageBuffers(sizeIn, sizeOut);
        compute.updateDescriptorSet();
        compute.fence = Fence::Create(true);
        compute.semaphore = Semaphore::Create();

        return compute;
    }

    std::vector<Compute> Compute::Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut, uint32_t count)
    {
        CreateResources();

        std::vector<CommandBuffer*> commandBuffers(count);
        std::vector<Compute> computes(count);

        for (auto& commandBuffer : commandBuffers)
        {
            Compute compute;
            compute.commandBuffer = CommandBuffer::Create(s_commandPool);
            compute.createPipeline(shaderName);
            compute.createDescriptorSet();
            compute.createComputeStorageBuffers(sizeIn, sizeOut);
            compute.updateDescriptorSet();
            compute.fence = Fence::Create(true);
            compute.semaphore = Semaphore::Create();

            computes.push_back(compute);
        }

        return computes;
    }

    void Compute::CreateResources()
    {
        if (!s_commandPool)
            s_commandPool = CommandPool::Create(RHII.computeFamilyId);
    }

    void Compute::DestroyResources()
    {
        s_commandPool->Destroy();
        Pipeline::getDescriptorSetLayoutCompute()->Destroy();
    }
}
#endif
