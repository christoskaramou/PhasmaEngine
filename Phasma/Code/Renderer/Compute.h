#pragma once

#include "Buffer.h"
#include "Pipeline.h"
#include <vector>

namespace vk
{
	class Fence;
	
	class DescriptorSet;
	
	class DescriptorSetLayout;
	
	class CommandBuffer;
	
	class CommandPool;
}

namespace pe
{
	constexpr uint32_t AUTO = UINT32_MAX;
	
	class Compute
	{
	public:
		static Compute Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut);
		
		static std::vector<Compute>
		Create(const std::string& shaderName, size_t sizeIn, size_t sizeOut, uint32_t count);
		
		static void CreateResources();
		
		static void DestroyResources();
		
		Compute();
		
		~Compute() = default;
		
		void updateInput(const void* srcData, size_t srcSize = 0, size_t offset = 0);
		
		void dispatch(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ, const std::vector<vk::Semaphore>& waitFor = {});
		
		void waitFence();
		
		void destroy();
		
		template<typename T, uint32_t N>
		inline std::vector<T> copyOutput(size_t range = 0, size_t offset = 0)
		{
			static_assert(N > 0);
			
			size_t n;
			if constexpr (N == AUTO)
			{
				n = SBOut.SizeRequested() / sizeof(T);
				if (n < 1)
					throw std::range_error("The size requested in Compute::copyOutput is bigger than the buffer");
			}
			else
			{
				n = N;
				if (n * sizeof(T) > SBOut.SizeRequested())
					throw std::range_error("The size requested in Compute::copyOutput is bigger than the buffer");
			}
			
			if (SBOut.SizeRequested() % sizeof(T) != 0)
				throw std::range_error(
						"The type of T is not dividing the buffer size accurately in Compute::copyOutput"
				);
			
			std::vector<T> output(n);
			SBOut.Map(range, offset);
			T* data = static_cast<T*>(SBOut.Data());
			for (int i = 0; i < n; ++i)
			{
				output[i] = data[i];
			}
			SBOut.Unmap();
			
			return output;
		}
		
		template<typename T>
		inline T copyOutput(size_t range = 0, size_t offset = 0)
		{
			assert(sizeof(T) <= SBOut.Size());
			
			SBOut.Map(range, offset);
			T output = *static_cast<T*>(SBOut.Data());
			SBOut.Unmap();
			
			return output;
		}
		
		void createPipeline(const std::string& shaderName);
	
	private:
		static Ref<vk::CommandPool> s_commandPool;
		Buffer SBIn;
		Buffer SBOut;
		Pipeline pipeline;
		Ref<vk::Fence> fence;
		Ref<vk::Semaphore> semaphore;
		Ref<vk::DescriptorSet> DSCompute;
		Ref<vk::CommandBuffer> commandBuffer;
		
		void createComputeStorageBuffers(size_t sizeIn, size_t sizeOut);
		
		void createDescriptorSet();
		
		void updateDescriptorSet();
	};
}