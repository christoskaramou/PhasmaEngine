#include "PhasmaPch.h"
#include "Compute.h"
#include "../Shader/Shader.h"
#include "RenderApi.h"
#include <deque>
#include <cassert>

namespace pe
{
	Ref<vk::CommandPool> Compute::s_commandPool = make_ref(vk::CommandPool());
	
	Compute::Compute()
	{
		fence = make_ref(vk::Fence());
		semaphore = make_ref(vk::Semaphore());
		DSCompute = make_ref(vk::DescriptorSet());
		commandBuffer = make_ref(vk::CommandBuffer());
	}
	
	void Compute::updateInput(const void* srcData, size_t srcSize, size_t offset)
	{
		SBIn.Map();
		SBIn.CopyData(srcData, srcSize, offset);
		SBIn.Flush();
		SBIn.Unmap();
	}
	
	void Compute::dispatch(
			const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ, const std::vector<vk::Semaphore>& waitFor
	)
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
		VulkanContext::Get()->computeQueue->submit(siCompute, *fence);
	}
	
	void Compute::waitFence()
	{
		VulkanContext::Get()->waitFences(*fence);
	}
	
	void Compute::createComputeStorageBuffers(size_t sizeIn, size_t sizeOut)
	{
		SBIn.CreateBuffer(sizeIn, BufferUsage::StorageBuffer, MemoryProperty::HostVisible);
		SBIn.Map();
		SBIn.Zero();
		SBIn.Flush();
		SBIn.Unmap();
		
		SBOut.CreateBuffer(sizeOut, BufferUsage::StorageBuffer, MemoryProperty::HostVisible);
		SBOut.Map();
		SBOut.Zero();
		SBOut.Flush();
		SBOut.Unmap();
	}
	
	void Compute::createDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutCompute();
		DSCompute = make_ref(VulkanContext::Get()->device->allocateDescriptorSets(allocInfo).at(0));
	}
	
	void Compute::updateDescriptorSet()
	{
		std::deque<vk::DescriptorBufferInfo> dsbi {};
		auto const wSetBuffer = [&dsbi](
				const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer, vk::DescriptorType type
		)
		{
			dsbi.emplace_back(*buffer.GetBufferVK(), 0, buffer.Size());
			return vk::WriteDescriptorSet {dstSet, dstBinding, 0, 1, type, nullptr, &dsbi.back(), nullptr};
		};
		std::vector<vk::WriteDescriptorSet> writeCompDescriptorSets
				{
						wSetBuffer(*DSCompute, 0, SBIn, vk::DescriptorType::eStorageBuffer),
						wSetBuffer(*DSCompute, 1, SBOut, vk::DescriptorType::eStorageBuffer),
				};
		VulkanContext::Get()->device->updateDescriptorSets(writeCompDescriptorSets, nullptr);
	}
	
	void Compute::createPipeline(const std::string& shaderName)
	{
		pipeline.destroy();
		
		Shader shader {shaderName, ShaderType::Compute, true};
		
		pipeline.info.pCompShader = &shader;
		pipeline.info.descriptorSetLayouts = make_ref(
				std::vector<vk::DescriptorSetLayout> {Pipeline::getDescriptorSetLayoutCompute()}
		);
		
		pipeline.createComputePipeline();
		
	}
	
	void Compute::destroy()
	{
		SBIn.Destroy();
		SBOut.Destroy();
		pipeline.destroy();
		if (*fence)
		{
			VulkanContext::Get()->device->destroyFence(*fence);
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
		vk::CommandBuffer commandBuffer = VulkanContext::Get()->device->allocateCommandBuffers(cbai).at(0);
		
		Compute compute;
		compute.commandBuffer = make_ref(commandBuffer);
		compute.createPipeline(shaderName);
		compute.createDescriptorSet();
		compute.createComputeStorageBuffers(sizeIn, sizeOut);
		compute.updateDescriptorSet();
		compute.fence = make_ref(
				VulkanContext::Get()->device->createFence(vk::FenceCreateInfo {vk::FenceCreateFlagBits::eSignaled})
		);
		compute.semaphore = make_ref(VulkanContext::Get()->device->createSemaphore(vk::SemaphoreCreateInfo {}));
		
		return compute;
	}
	
	std::vector<Compute> Compute::Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut, uint32_t count)
	{
		CreateResources();
		
		vk::CommandBufferAllocateInfo cbai;
		cbai.commandPool = *s_commandPool;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandBufferCount = count;
		std::vector<vk::CommandBuffer> commandBuffers = VulkanContext::Get()->device->allocateCommandBuffers(cbai);
		
		std::vector<Compute> computes(count);
		
		for (auto& commandBuffer : commandBuffers)
		{
			Compute compute;
			compute.commandBuffer = make_ref(commandBuffer);
			compute.createPipeline(shaderName);
			compute.createDescriptorSet();
			compute.createComputeStorageBuffers(sizeIn, sizeOut);
			compute.updateDescriptorSet();
			compute.fence = make_ref(
					VulkanContext::Get()->device->createFence(vk::FenceCreateInfo {vk::FenceCreateFlagBits::eSignaled})
			);
			
			computes.push_back(compute);
		}
		
		return computes;
	}
	
	void Compute::CreateResources()
	{
		if (!*s_commandPool)
		{
			vk::CommandPoolCreateInfo cpci;
			cpci.queueFamilyIndex = VulkanContext::Get()->computeFamilyId;
			cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
			s_commandPool = make_ref(VulkanContext::Get()->device->createCommandPool(cpci));
		}
	}
	
	void Compute::DestroyResources()
	{
		if (*s_commandPool)
		{
			VulkanContext::Get()->device->destroyCommandPool(*s_commandPool);
			*s_commandPool = nullptr;
		}
		
		if (Pipeline::getDescriptorSetLayoutCompute())
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutCompute());
			Pipeline::getDescriptorSetLayoutCompute() = nullptr;
		}
	}
}
