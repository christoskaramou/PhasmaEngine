#pragma once

#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../VulkanContext/VulkanContext.h"
#include <functional>
#include <deque>

namespace vm {
	struct Compute
	{
		Buffer& getIn();
		Buffer& getOut();
		void dispatch(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
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

		static vk::DescriptorSetLayout* getDescriptorLayout();
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
		void waitFences();
		void destroy();

		static ComputePool& get() noexcept { static ComputePool cp; return cp; }
	private:
		ComputePool() {};								// default constructor
		ComputePool(ComputePool const&) {};				// copy constructor
		ComputePool operator=(ComputePool const&) {};	// copy assignment
		ComputePool(ComputePool&&) noexcept {};			// move constructor
		ComputePool operator=(ComputePool&&) {};		// move assignment
		~ComputePool() {};								// destructor
	};
}