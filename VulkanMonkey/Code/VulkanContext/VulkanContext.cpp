#include "VulkanContext.h"
#include <vulkan/vulkan.hpp>

namespace vm
{
	VulkanContext::VulkanContext()
	{
		instance = vk::Instance();
		debugMessenger = vk::DebugUtilsMessengerEXT();
		gpu = vk::PhysicalDevice();
		gpuProperties = vk::PhysicalDeviceProperties();
		gpuFeatures = vk::PhysicalDeviceFeatures();
		device = vk::Device();
		graphicsQueue = vk::Queue();
		computeQueue = vk::Queue();
		transferQueue = vk::Queue();
		commandPool = vk::CommandPool();
		commandPool2 = vk::CommandPool();
		descriptorPool = vk::DescriptorPool();
		dispatchLoaderDynamic = vk::DispatchLoaderDynamic();
		queueFamilyProperties = std::vector<vk::QueueFamilyProperties>();
		dynamicCmdBuffers = std::vector<vk::CommandBuffer>();
		shadowCmdBuffers = std::vector<vk::CommandBuffer>();
		fences = std::vector<vk::Fence>();
		semaphores = std::vector<vk::Semaphore>();

		window = nullptr;
		graphicsFamilyId = 0;
		computeFamilyId = 0;
		transferFamilyId = 0;
	}

	void VulkanContext::submit(
		const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
		const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
		const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
		const vk::ArrayProxy<const vk::Semaphore> signalSemaphores,
		const vk::Fence signalFence) const
	{
		vk::SubmitInfo si;
		si.waitSemaphoreCount = waitSemaphores.size();
		si.pWaitSemaphores = waitSemaphores.data();
		si.pWaitDstStageMask = waitStages.data();
		si.commandBufferCount = commandBuffers.size();
		si.pCommandBuffers = commandBuffers.data();
		si.signalSemaphoreCount = signalSemaphores.size();
		si.pSignalSemaphores = signalSemaphores.data();
		graphicsQueue->submit(si, signalFence);
	}
	void VulkanContext::waitFences(const vk::ArrayProxy<const vk::Fence> fences) const
	{
		if (device->waitForFences(fences, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
			throw std::runtime_error("wait fences error!");
		device->resetFences(fences);
	}

	void VulkanContext::submitAndWaitFence(
		const vk::ArrayProxy<const vk::CommandBuffer> commandBuffers,
		const vk::ArrayProxy<const vk::PipelineStageFlags> waitStages,
		const vk::ArrayProxy<const vk::Semaphore> waitSemaphores,
		const vk::ArrayProxy<const vk::Semaphore> signalSemaphores) const
	{
		const vk::FenceCreateInfo fi;
		const vk::Fence fence = device->createFence(fi);

		submit(commandBuffers, waitStages, waitSemaphores, signalSemaphores, fence);

		if (device->waitForFences(fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
			throw std::runtime_error("wait fences error!");
		device->destroyFence(fence);
	}

	void VulkanContext::waitAndLockSubmits()
	{
		m_submit_mutex.lock();
	}

	void VulkanContext::unlockSubmits()
	{
		m_submit_mutex.unlock();
	}

	VulkanContext* VulkanContext::get() noexcept
	{
		static auto VkCTX = new VulkanContext(); return VkCTX;
	}

	void VulkanContext::remove() noexcept
	{
		if (get())
			delete get();
	}
}
