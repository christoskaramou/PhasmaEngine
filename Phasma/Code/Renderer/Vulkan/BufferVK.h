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
#include "Renderer/Buffer.h"

struct VkBuffer_T;
namespace pe
{
	class BufferVK : public Buffer
	{
	public:
		BufferVK(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties);

		~BufferVK();
		
		void Map() override;
		
		void Unmap() override;
		
		void Zero() const override;
		
		void CopyData(const void* srcData, size_t srcSize = 0, size_t offset = 0) override;
		
		void CopyBuffer(Buffer* srcBuffer, size_t srcSize = 0) const override;
		
		void Flush(size_t offset = 0, size_t flushSize = 0) const override;
		
		void Destroy() override;

		void SetDebugName(const std::string& debugName) override;

		size_t Size() override;

		size_t SizeRequested() override;

		void* Data() override;

	protected:
		void* Handle() override;

	private:
		VkBuffer_T* handle;
		VmaAllocation allocation;
		size_t size{};
		// sizeRequested is the size asked from user during the creation, afterwards the size can be
		// changed dynamically based on the system's minimum alignment on the specific buffer type
		// and it will always be less or equal than the actual size of the created buffer
		size_t sizeRequested{};

		void* data = nullptr;

	};
}