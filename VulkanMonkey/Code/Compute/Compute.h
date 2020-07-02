#pragma once
#include "../Core/Buffer.h"
#include "../Renderer/Pipeline.h"
#include <deque>

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
		Ref<vk::Fence> fence;
		Ref<vk::DescriptorSet> DSCompute;
		Ref<vk::CommandBuffer> commandBuffer;

		void createPipeline();
		void createComputeStorageBuffers(size_t sizeIn, size_t sizeOut);
		void createDescriptorSet();
		void updateDescriptorSet();
		void destroy();
	};

	class ComputePool
	{
	public:
		Ref<vk::CommandPool> commandPool;
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