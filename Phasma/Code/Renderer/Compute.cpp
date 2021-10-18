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
	SPtr<vk::CommandPool> Compute::s_commandPool = make_sptr(vk::CommandPool());
	
	Compute::Compute()
	{
		fence = make_sptr(vk::Fence());
		semaphore = make_sptr(vk::Semaphore());
		DSCompute = make_sptr(vk::DescriptorSet());
		commandBuffer = make_sptr(vk::CommandBuffer());
	}
	
	void Compute::updateInput(const void* srcData, size_t srcSize, size_t offset)
	{
		SBIn->Map();
		SBIn->CopyData(srcData, srcSize, offset);
		SBIn->Flush();
		SBIn->Unmap();
	}
	
	void Compute::dispatch(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ, const std::vector<vk::Semaphore>& waitFor)
	{
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		
		auto& cmd = *commandBuffer;
		cmd.begin(beginInfo);
		
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline.layout, 0, *DSCompute, nullptr);
		cmd.dispatch(sizeX, sizeY, sizeZ);
		
		cmd.end();
		
		vk::SubmitInfo siCompute;
		siCompute.commandBufferCount = 1;
		siCompute.pCommandBuffers = &cmd;
		siCompute.pWaitSemaphores = !waitFor.empty() ? waitFor.data() : nullptr;
		siCompute.pSignalSemaphores = semaphore.get();
		VULKAN.computeQueue->submit(siCompute, *fence);
	}
	
	void Compute::waitFence()
	{
		VULKAN.waitFences(*fence);
	}
	
	void Compute::createComputeStorageBuffers(size_t sizeIn, size_t sizeOut)
	{
		SBIn = Buffer::Create(
			sizeIn,
			(BufferUsageFlags)vk::BufferUsageFlagBits::eStorageBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		SBIn->Map();
		SBIn->Zero();
		SBIn->Flush();
		SBIn->Unmap();
		SBIn->SetDebugName("Compute_SB_In");
		
		SBOut = Buffer::Create(
			sizeOut,
			(BufferUsageFlags)vk::BufferUsageFlagBits::eStorageBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		SBOut->Map();
		SBOut->Zero();
		SBOut->Flush();
		SBOut->Unmap();
		SBOut->SetDebugName("Compute_SB_Out");
	}
	
	void Compute::createDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *VULKAN.descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutCompute();
		DSCompute = make_sptr(VULKAN.device->allocateDescriptorSets(allocInfo).at(0));
		VULKAN.SetDebugObjectName(*DSCompute, "Compute");
	}
	
	void Compute::updateDescriptorSet()
	{
		std::deque<vk::DescriptorBufferInfo> dsbi {};
		auto const wSetBuffer = [&dsbi](
				const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer, vk::DescriptorType type
		)
		{
			dsbi.emplace_back(buffer.Handle<vk::Buffer>(), 0, buffer.Size());
			return vk::WriteDescriptorSet {dstSet, dstBinding, 0, 1, type, nullptr, &dsbi.back(), nullptr};
		};
		std::vector<vk::WriteDescriptorSet> writeCompDescriptorSets
				{
						wSetBuffer(*DSCompute, 0, *SBIn, vk::DescriptorType::eStorageBuffer),
						wSetBuffer(*DSCompute, 1, *SBOut, vk::DescriptorType::eStorageBuffer),
				};
		VULKAN.device->updateDescriptorSets(writeCompDescriptorSets, nullptr);
	}
	
	void Compute::createPipeline(const std::string& shaderName)
	{
		pipeline.destroy();
		
		Shader shader {shaderName, ShaderType::Compute, true};
		
		pipeline.info.pCompShader = &shader;
		pipeline.info.descriptorSetLayouts = make_sptr(
				std::vector<vk::DescriptorSetLayout> {(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutCompute()}
		);
		
		pipeline.createComputePipeline();
		VULKAN.SetDebugObjectName(*pipeline.handle, "Compute");
	}
	
	void Compute::destroy()
	{
		SBIn->Destroy();
		SBOut->Destroy();
		pipeline.destroy();
		if (*semaphore)
		{
			VULKAN.device->destroySemaphore(*semaphore);
			*semaphore = nullptr;
		}
		if (*fence)
		{
			VULKAN.device->destroyFence(*fence);
			*fence = nullptr;
		}
	}
	
	Compute Compute::Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut)
	{
		CreateResources();
		
		vk::CommandBufferAllocateInfo cbai;
		cbai.commandPool = *s_commandPool;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandBufferCount = 1;
		vk::CommandBuffer commandBuffer = VULKAN.device->allocateCommandBuffers(cbai).at(0);
		VULKAN.SetDebugObjectName(commandBuffer, "Compute");
		
		Compute compute;
		compute.commandBuffer = make_sptr(commandBuffer);
		compute.createPipeline(shaderName);
		compute.createDescriptorSet();
		compute.createComputeStorageBuffers(sizeIn, sizeOut);
		compute.updateDescriptorSet();
		compute.fence = make_sptr(
				VULKAN.device->createFence(vk::FenceCreateInfo {vk::FenceCreateFlagBits::eSignaled})
		);
		compute.semaphore = make_sptr(VULKAN.device->createSemaphore(vk::SemaphoreCreateInfo{}));
		VULKAN.SetDebugObjectName(*compute.fence, "Compute");
		VULKAN.SetDebugObjectName(*compute.semaphore, "Compute");
		
		return compute;
	}
	
	std::vector<Compute> Compute::Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut, uint32_t count)
	{
		CreateResources();
		
		vk::CommandBufferAllocateInfo cbai;
		cbai.commandPool = *s_commandPool;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandBufferCount = count;
		std::vector<vk::CommandBuffer> commandBuffers = VULKAN.device->allocateCommandBuffers(cbai);
		
		std::vector<Compute> computes(count);
		
		for (auto& commandBuffer : commandBuffers)
		{
			VULKAN.SetDebugObjectName(commandBuffer, "Compute");

			Compute compute;
			compute.commandBuffer = make_sptr(commandBuffer);
			compute.createPipeline(shaderName);
			compute.createDescriptorSet();
			compute.createComputeStorageBuffers(sizeIn, sizeOut);
			compute.updateDescriptorSet();
			compute.fence = make_sptr(
					VULKAN.device->createFence(vk::FenceCreateInfo {vk::FenceCreateFlagBits::eSignaled})
			);
			compute.semaphore = make_sptr(VULKAN.device->createSemaphore(vk::SemaphoreCreateInfo{}));

			VULKAN.SetDebugObjectName(*compute.fence, "Compute");
			VULKAN.SetDebugObjectName(*compute.semaphore, "Compute");
			
			computes.push_back(compute);
		}
		
		return computes;
	}
	
	void Compute::CreateResources()
	{
		if (!*s_commandPool)
		{
			vk::CommandPoolCreateInfo cpci;
			cpci.queueFamilyIndex = VULKAN.computeFamilyId;
			cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
			s_commandPool = make_sptr(VULKAN.device->createCommandPool(cpci));
			VULKAN.SetDebugObjectName(*s_commandPool, "Compute");
		}
	}
	
	void Compute::DestroyResources()
	{
		if (*s_commandPool)
		{
			VULKAN.device->destroyCommandPool(*s_commandPool);
			*s_commandPool = nullptr;
		}
		
		if (Pipeline::getDescriptorSetLayoutCompute())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutCompute(), nullptr);
			Pipeline::getDescriptorSetLayoutCompute() = {};
		}
	}
}
