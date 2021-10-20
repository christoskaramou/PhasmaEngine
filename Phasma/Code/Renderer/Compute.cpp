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

#include "PhasmaPch.h"
#include "Compute.h"
#include "Code/Shader/Shader.h"
#include "Renderer/Vulkan/Vulkan.h"
#include <deque>
#include <cassert>

namespace pe
{
	CommandPool Compute::s_commandPool = {};
	
	Compute::Compute()
	{
		fence = {};
		semaphore = {};
		DSCompute = {};
		commandBuffer = {};
	}
	
	void Compute::updateInput(const void* srcData, size_t srcSize, size_t offset)
	{
		SBIn->Map();
		SBIn->CopyData(srcData, srcSize, offset);
		SBIn->Flush();
		SBIn->Unmap();
	}
	
	void Compute::dispatch(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ, uint32_t count, SemaphoreHandle* waitForHandles)
	{
		commandBuffer.Begin();
		commandBuffer.BindComputePipeline(pipeline);
		commandBuffer.BindComputeDescriptors(pipeline, 1, &DSCompute);
		commandBuffer.Dispatch(sizeX, sizeY, sizeZ);
		commandBuffer.End();
		
		std::vector<VkSemaphore> waitSemaphores = ApiHandleVectorCopy<VkSemaphore>(count, waitForHandles);
		VkCommandBuffer cmdBuffer = commandBuffer.Handle();
		VkSemaphore vksemaphore = semaphore.handle;
		VkSubmitInfo siCompute{};
		siCompute.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		siCompute.commandBufferCount = 1;
		siCompute.pCommandBuffers = &cmdBuffer;
		siCompute.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
		siCompute.pWaitSemaphores = waitSemaphores.data();
		siCompute.pSignalSemaphores = &vksemaphore;

		vkQueueSubmit(*VULKAN.computeQueue, 1, &siCompute, fence.handle);
	}
	
	void Compute::waitFence()
	{
		VULKAN.waitFences(vk::Fence(fence.handle));
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
		VkDescriptorSetLayout dsetLayout = Pipeline::getDescriptorSetLayoutCompute();
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = *VULKAN.descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &dsetLayout;

		VkDescriptorSet dset;
		vkAllocateDescriptorSets(*VULKAN.device, &allocInfo, &dset);
		DSCompute = dset;
	}
	
	void Compute::updateDescriptorSet()
	{
		std::deque<VkDescriptorBufferInfo> dsbi {};
		auto const wSetBuffer = [&dsbi](DescriptorSetHandle dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			VkDescriptorBufferInfo info{ buffer.Handle(), 0, buffer.Size() };
			dsbi.push_back(info);

			VkWriteDescriptorSet writeSet{};
			writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeSet.dstSet = dstSet;
			writeSet.dstBinding = dstBinding;
			writeSet.dstArrayElement = 0;
			writeSet.descriptorCount = 1;
			writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writeSet.pBufferInfo = &dsbi.back();

			return writeSet;
		};

		std::vector<VkWriteDescriptorSet> writeSets
		{
			wSetBuffer(DSCompute, 0, *SBIn),
			wSetBuffer(DSCompute, 1, *SBOut),
		};

		vkUpdateDescriptorSets(*VULKAN.device, (uint32_t)writeSets.size(), writeSets.data(), 0, nullptr);
	}
	
	void Compute::createPipeline(const std::string& shaderName)
	{
		pipeline.destroy();
		
		Shader shader {shaderName, ShaderType::Compute, true};
		
		pipeline.info.pCompShader = &shader;
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutCompute() };
		
		pipeline.createComputePipeline();
	}
	
	void Compute::destroy()
	{
		SBIn->Destroy();
		SBOut->Destroy();
		pipeline.destroy();
		semaphore.Destroy();
		fence.Destroy();
	}
	
	Compute Compute::Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut)
	{
		CreateResources();
		
		Compute compute;
		compute.commandBuffer.Create(s_commandPool);
		compute.createPipeline(shaderName);
		compute.createDescriptorSet();
		compute.createComputeStorageBuffers(sizeIn, sizeOut);
		compute.updateDescriptorSet();
		compute.fence.Create(true);
		compute.semaphore.Create();
		
		return compute;
	}
	
	std::vector<Compute> Compute::Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut, uint32_t count)
	{
		CreateResources();
		
		std::vector<CommandBuffer> commandBuffers(count);
		std::vector<Compute> computes(count);
		
		for (auto& commandBuffer : commandBuffers)
		{
			Compute compute;
			compute.commandBuffer.Create(s_commandPool);
			compute.createPipeline(shaderName);
			compute.createDescriptorSet();
			compute.createComputeStorageBuffers(sizeIn, sizeOut);
			compute.updateDescriptorSet();
			compute.fence.Create(true);
			compute.semaphore.Create();
			
			computes.push_back(compute);
		}
		
		return computes;
	}
	
	void Compute::CreateResources()
	{
		if (!s_commandPool.Handle())
			s_commandPool.Create(VULKAN.computeFamilyId);
	}
	
	void Compute::DestroyResources()
	{
		s_commandPool.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutCompute())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutCompute(), nullptr);
			Pipeline::getDescriptorSetLayoutCompute() = {};
		}
	}
}
