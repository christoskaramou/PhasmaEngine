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

#pragma once

#include "Buffer.h"
#include "Pipeline.h"
#include <vector>

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
		
		template<class T>
		void copyDataTo(T* ptr, size_t elements)
		{
			assert(elements * sizeof(T) <= SBOut.SizeRequested());

			SBOut.Map();
			memcpy(ptr, SBOut.Data(), elements * sizeof(T));
			SBOut.Unmap();
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