#pragma once

#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../VulkanContext/VulkanContext.h"
#include <functional>
#include <deque>

namespace vm {
	struct Compute
	{
		void setUpdate(std::function<void()>&& func);
		void update();
		void dispatch(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ);
		void waitFence();
	private:
		friend struct ComputePool;
		VulkanContext* vulkan = &VulkanContext::get();

		bool ready = true;

		Buffer SBIn;
		Buffer SBOut;
		vk::Fence fence;
		vk::DescriptorSet DSCompute;
		static vk::DescriptorSetLayout DSLayoutCompute;
		Pipeline pipeline;
		vk::CommandBuffer commandBuffer;

		static vk::DescriptorSetLayout getDescriptorLayout();
		std::function<void()> updateFunc;
		void createPipeline();
		void createComputeStorageBuffers(size_t sizeIn, size_t sizeOut);
		void createDescriptorSet();
		void updateDescriptorSet();
		void destroy();
	};
	struct ComputePool
	{
		vk::CommandPool commandPool;
		std::deque<Compute> compute{};
		void Init(uint32_t cmdBuffersCount);
		Compute& getNext();
		void destroy();

		static ComputePool& get() { static ComputePool cp; return cp; }
	private:
		ComputePool() {};
		ComputePool(ComputePool const&) {};
		ComputePool& operator=(ComputePool const&) {};
		~ComputePool() {};
	};
}