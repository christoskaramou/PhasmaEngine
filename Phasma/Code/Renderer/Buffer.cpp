#include "Buffer.h"
#include "RenderApi.h"
#include "Vulkan/BufferVK.h"
//#include "Dx12/BufferDX.h"

namespace pe
{
	Buffer::Buffer()
	{
		m_bufferVK = make_ref(BufferVK());
		//m_bufferDX = make_ref(BufferDX());
	}
	
	void Buffer::CreateBuffer(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties)
	{
		if (PE_VULKAN)
		{
			m_bufferVK->CreateBuffer(size, usage, properties);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Map(size_t mapSize, size_t offset)
	{
		if (PE_VULKAN)
		{
			m_bufferVK->Map(mapSize, offset);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Unmap()
	{
		if (PE_VULKAN)
		{
			m_bufferVK->Unmap();
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Zero() const
	{
		if (PE_VULKAN)
		{
			m_bufferVK->Zero();
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::CopyData(const void* srcData, size_t srcSize, size_t offset)
	{
		if (PE_VULKAN)
		{
			m_bufferVK->CopyData(srcData, srcSize, offset);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::CopyBuffer(Buffer* srcBuffer, size_t srcSize) const
	{
		if (PE_VULKAN)
		{
			m_bufferVK->CopyBuffer(*srcBuffer->m_bufferVK->buffer, srcSize);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Flush(size_t offset, size_t flushSize) const
	{
		if (PE_VULKAN)
		{
			m_bufferVK->Flush(offset, flushSize);
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	void Buffer::Destroy() const
	{
		if (PE_VULKAN)
		{
			m_bufferVK->Destroy();
		}
		else //if (PE_DX12)
		{
		
		}
	}
	
	size_t Buffer::Size()
	{
		if (PE_VULKAN)
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
		if (PE_VULKAN)
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
		if (PE_VULKAN)
		{
			return m_bufferVK->data;
		}
		else //if (PE_DX12)
		{
			return nullptr;
		}
	}
	
	// TEMPORARY
	Ref<vk::Buffer> Buffer::GetBufferVK()
	{
		return m_bufferVK->buffer;
	}
}
