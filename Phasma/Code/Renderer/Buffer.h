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
#include <MemoryHash/MemoryHash.h>
#include <Core/Queue.h>

namespace pe
{
	using MemoryPropertyFlags = uint64_t;
	using BufferUsageFlags = uint64_t;

	class Buffer : public NoCopy, public NoMove
	{
	public:
		static SPtr<Buffer> Create(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties);

		virtual ~Buffer() {};
		
		virtual void Map() = 0;
		
		virtual void Unmap() = 0;
		
		virtual void Zero() const = 0;
		
		virtual void CopyData(const void* srcData, size_t srcSize = 0, size_t offset = 0) = 0;
		
		virtual void CopyBuffer(Buffer* srcBuffer, size_t srcSize = 0) const = 0;
		
		virtual void Flush(size_t offset = 0, size_t flushSize = 0) const = 0;
		
		virtual void Destroy() = 0;
		
		virtual size_t Size() = 0;
		
		virtual size_t SizeRequested() = 0;
		
		virtual void* Data() = 0;

		virtual void SetDebugName(const std::string& debugName) = 0;

	protected:
		virtual void* Handle() = 0;

	public:
		template<class T>
		T& Handle() { return *static_cast<T*>(Handle()); }

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
	};
}