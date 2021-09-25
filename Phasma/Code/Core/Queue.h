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

#include <string>
#include <future>
#include <tuple>
#include <deque>
#include <any>
#include <mutex>
#include "../Renderer/Buffer.h"
#include "../MemoryHash/MemoryHash.h"

namespace pe
{
	enum class CopyRequestType
	{
		Async,
		AsyncDeferred,
		Sync,
		SyncDeferred,
	};

	class CopyRequest
	{
	public:
		Buffer* buffer;
		std::vector<MemoryRange> memory_ranges {};
		
		void exec_mem_copy()
		{
			buffer->Map();
			for (auto& memory_range : memory_ranges)
				buffer->CopyData(memory_range.data, memory_range.size, memory_range.offset);
			buffer->Flush();
			buffer->Unmap();
		}
	};
	
	class Queue
	{
	public:
		inline static std::deque<std::tuple<int, std::string>> addScript {};
		inline static std::deque<int> removeScript {};
		inline static std::deque<int> compileScript {};
	private:
		inline static std::deque<CopyRequest> copy_requests{};
		inline static std::deque<CopyRequest> sync_deferred_requests{};
		inline static std::mutex mem_cpy_request_mutex {};
		inline static std::vector<std::future<void>> futureNodes{};
	public:
		inline static void memcpyRequest(Buffer* buffer, const std::vector<MemoryRange>& ranges, CopyRequestType type = CopyRequestType::AsyncDeferred)
		{
			std::lock_guard<std::mutex> guard(mem_cpy_request_mutex);
			switch (type)
			{
			case CopyRequestType::Async:
				copy_requests.push_back({ buffer, ranges });
				futureNodes.push_back(std::async(std::launch::async, std::bind(&CopyRequest::exec_mem_copy, copy_requests.back())));
				break;
			case CopyRequestType::AsyncDeferred:
				copy_requests.push_back({ buffer, ranges });
				futureNodes.push_back(std::async(std::launch::deferred, std::bind(&CopyRequest::exec_mem_copy, copy_requests.back())));
				break;
			case CopyRequestType::Sync:
				copy_requests.push_back({ buffer, ranges });
				copy_requests.back().exec_mem_copy();
				break;
			case CopyRequestType::SyncDeferred:
				sync_deferred_requests.push_back({ buffer, ranges });
				break;
			}
		}

		inline static void exec_memcpyRequests()
		{
			for (auto& futureNode : futureNodes)
				futureNode.get();

			for (auto& syncDeferred : sync_deferred_requests)
				syncDeferred.exec_mem_copy();

			futureNodes.clear();
			copy_requests.clear();
			sync_deferred_requests.clear();
		}
	};
}