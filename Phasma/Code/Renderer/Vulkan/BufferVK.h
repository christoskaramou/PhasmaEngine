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
#include "Renderer/RendererEnums.h"

namespace pe
{
	class BufferVK
	{
	public:
		BufferVK();
		
		Ref<vk::Buffer> buffer;
		VmaAllocation allocation;
		size_t size{};
		// sizeRequested is the size asked from user during the creation, afterwards the size can be
		// changed dynamically based on the system's minimum alignment on the specific buffer type
		// and it will always be less or equal than the actual size of the created buffer
		size_t sizeRequested {};
		void* data = nullptr;
		
		void CreateBuffer(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties);
		
		void Map();
		
		void Unmap();
		
		void Zero() const;
		
		void CopyData(const void* srcData, size_t srcSize = 0, size_t offset = 0);
		
		void CopyBuffer(vk::Buffer srcBuffer, size_t srcSize = 0) const;
		
		void Flush(size_t offset = 0, size_t flushSize = 0) const;
		
		void Destroy() const;

		void SetDebugName(const std::string& debugName);
	};
}