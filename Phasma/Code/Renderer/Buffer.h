#pragma once

#include "../Core/Base.h"

// TEMPORARY
namespace vk
{
	class Buffer;
}

namespace pe
{
	enum MemoryProperty
	{
		DeviceLocal,
		HostVisible,
		HostCoherent,
		HostCached
	};
	
	enum BufferUsage
	{
		TransferSrc = 1,
		TransferDst = 1<<1,
		UniformTexelBuffer = 1<<2,
		StorageTexelBuffer = 1<<3,
		UniformBuffer = 1<<4,
		StorageBuffer = 1<<5,
		IndexBuffer = 1<<6,
		VertexBuffer = 1<<7
	};
	
	using BufferUsageFlags = Flags<BufferUsage>;
	using MemoryPropertyFlags = Flags<MemoryProperty>;
	
	class BufferVK;
	class BufferDX;
	
	class Buffer
	{
	public:
		Buffer();
		
		void CreateBuffer(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties);
		
		void Map(size_t mapSize = 0, size_t offset = 0);
		
		void Unmap();
		
		void Zero() const;
		
		void CopyData(const void* srcData, size_t srcSize = 0, size_t offset = 0);
		
		void CopyBuffer(Buffer* srcBuffer, size_t srcSize = 0) const;
		
		void Flush(size_t offset = 0, size_t flushSize = 0) const;
		
		void Destroy() const;
		
		size_t Size();
		
		size_t SizeRequested();
		
		void* Data();
		
		// TEMPORARY
		Ref<vk::Buffer> GetBufferVK();
		
	private:
		Ref<BufferVK> m_bufferVK;
		Ref<BufferDX> m_bufferDX;
	};
}