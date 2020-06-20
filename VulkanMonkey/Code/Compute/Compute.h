#pragma once
#include "../Core/Buffer.h"
#include "../Renderer/Pipeline.h"
#include <deque>
#include <memory>

namespace vk
{
	class Fence;
	class DescriptorSet;
	class DescriptorSetLayout;
	class CommandBuffer;
	class CommandPool;
}

namespace vm
{
	class Compute
	{
	public:
		Compute();
		~Compute();
		Buffer& getIn();
		Buffer& getOut();
		void dispatch(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
		void waitFence();
	private:
		friend class ComputePool;

		bool ready = true;

		Buffer SBIn;
		Buffer SBOut;
		Pipeline pipeline;
		Ref_t<vk::Fence> fence;
		Ref_t<vk::DescriptorSet> DSCompute;
		Ref_t<vk::DescriptorSetLayout> DSLayoutCompute;
		Ref_t<vk::CommandBuffer> commandBuffer;

		const vk::DescriptorSetLayout& getDescriptorLayout();
		void createPipeline();
		void createComputeStorageBuffers(size_t sizeIn, size_t sizeOut);
		void createDescriptorSet();
		void updateDescriptorSet();
		void destroy();
	};

	class ComputePool
	{
	public:
		Ref_t<vk::CommandPool> commandPool;
		std::deque<Compute> compute{};
		void Init(uint32_t cmdBuffersCount);
		Compute& getNext();
		void waitFences();
		void destroy();

		static auto get() noexcept { static auto cp = new ComputePool(); return cp; }
		static auto remove() noexcept { using type = decltype(get()); if (std::is_pointer<type>::value) delete get(); }

		ComputePool(ComputePool const&) = delete;				// copy constructor
		ComputePool(ComputePool&&) noexcept = delete;			// move constructor
		ComputePool& operator=(ComputePool const&) = delete;	// copy assignment
		ComputePool& operator=(ComputePool&&) = delete;			// move assignment
	private:
		ComputePool();											// default constructor
		~ComputePool() = default;								// destructor
	};
}