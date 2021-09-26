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

#include "Buffer.h"
#include "RenderApi.h"
#include "Vulkan/BufferVK.h"
//#include "Dx12/BufferDX.h"

namespace pe
{
	Buffer::Buffer()
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK = make_ref(BufferVK());
		}
		else //if (PE_DX12)
		{
		
		};
	}
	
	void Buffer::CreateBuffer(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties)
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->CreateBuffer(size, usage, properties);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Map()
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->Map();
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Unmap()
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->Unmap();
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Zero() const
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->Zero();
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::CopyData(const void* srcData, size_t srcSize, size_t offset)
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->CopyData(srcData, srcSize, offset);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::CopyBuffer(Buffer* srcBuffer, size_t srcSize) const
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->CopyBuffer(*srcBuffer->m_bufferVK->buffer, srcSize);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Flush(size_t offset, size_t flushSize) const
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->Flush(offset, flushSize);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Destroy() const
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			m_bufferVK->Destroy();
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	size_t Buffer::Size()
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			return m_bufferVK->size;
		}
		else //if (PE_DX12)
		{
			return 0;
		}
	}
	
	size_t Buffer::SizeRequested()
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			return m_bufferVK->sizeRequested;
		}
		else //if (PE_DX12)
		{
			return 0;
		}
	}
	
	void* Buffer::Data()
	{
		if DYNAMIC_CONSTEXPR (PE_VULKAN)
		{
			return m_bufferVK->data;
		}
		else //if (PE_DX12)
		{
			return nullptr;
		}
	}

	void Buffer::CopyRequest(QueueType type, const MemoryRange& range)
	{
		auto lambda = [this, range]()
		{
			Map();
			CopyData(range.data, range.size, range.offset);
			Flush();
			Unmap();
		};
		Queue<1>::Request(type, lambda);
	}

	void Buffer::CopyRequest(QueueType type, const std::vector<MemoryRange>& ranges)
	{
		auto lambda = [this, ranges]()
		{
			Map();
			for (auto& range : ranges)
				CopyData(range.data, range.size, range.offset);
			Flush();
			Unmap();
		};
		Queue<1>::Request(type, lambda);
	}
	
	Ref<vk::Buffer> Buffer::GetBufferVK()
	{
		return m_bufferVK->buffer;
	}

	void Buffer::SetDebugName(const std::string& debugName)
	{
		if DYNAMIC_CONSTEXPR(PE_VULKAN)
		{
			m_bufferVK->SetDebugName(debugName);
		}
		else //if (PE_DX12)
		{
		}
	}
}
