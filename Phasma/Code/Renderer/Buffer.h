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

#include "Core/Base.h"
#include "RendererEnums.h"
#include <MemoryHash/MemoryHash.h>
#include <Core/Queue.h>

namespace pe
{
	class BufferVK;
	class BufferDX;
	
	class Buffer
	{
	public:
		Buffer();
		
		void CreateBuffer(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties);
		
		void Map();
		
		void Unmap();
		
		void Zero() const;
		
		void CopyData(const void* srcData, size_t srcSize = 0, size_t offset = 0);
		
		void CopyBuffer(Buffer* srcBuffer, size_t srcSize = 0) const;
		
		void Flush(size_t offset = 0, size_t flushSize = 0) const;
		
		void Destroy() const;
		
		size_t Size();
		
		size_t SizeRequested();
		
		void* Data();
		
		Ref<vk::Buffer> GetBufferVK();

		void SetDebugName(const std::string& debugName);

		template<Launch launch>
		void CopyRequest(const MemoryRange& range)
		{
			auto lambda = [this, range]()
			{
				Map();
				CopyData(range.data, range.size, range.offset);
				Flush();
				Unmap();
			};
			Queue<launch>::Request(lambda);
		}

		template<Launch launch>
		void CopyRequest(const std::vector<MemoryRange>& ranges)
		{
			auto lambda = [this, ranges]()
			{
				Map();
				for (auto& range : ranges)
					CopyData(range.data, range.size, range.offset);
				Flush();
				Unmap();
			};
			Queue<launch>::Request(lambda);
		}
		
	private:
		Ref<BufferVK> m_bufferVK;
		Ref<BufferDX> m_bufferDX;
	};
}