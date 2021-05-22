#pragma once

#include <string>
#include <future>
#include <tuple>
#include <deque>
#include <any>
#include <mutex>
#include "Buffer.h"
#include "../MemoryHash/MemoryHash.h"

namespace pe
{
	class CopyRequest
	{
	public:
		Buffer* buffer;
		std::vector<MemoryRange> memory_ranges {};
		
		void exec_mem_copy()
		{
			buffer->map();
			for (auto& memory_range : memory_ranges)
				buffer->copyData(memory_range.data, memory_range.size, memory_range.offset);
			buffer->flush();
			buffer->unmap();
		}
	};
	
	class Queue
	{
	public:
		// std::deque insertion and deletion at either end of a deque never invalidates pointers or references to the rest of the elements
		inline static std::deque<std::tuple<std::string, std::string>> loadModel {};
		inline static std::deque<int> unloadModel {};
		inline static std::deque<std::tuple<int, std::string>> addScript {};
		inline static std::deque<int> removeScript {};
		inline static std::deque<int> compileScript {};
		inline static std::deque<std::future<std::any>> loadModelFutures {};
	private:
		inline static std::vector<CopyRequest> m_async_copy_requests {};
		inline static std::mutex m_mem_cpy_request_mutex {};
	public:
		inline static void memcpyRequest(Buffer* buffer, const std::vector<MemoryRange>& ranges)
		{
			std::lock_guard<std::mutex> guard(m_mem_cpy_request_mutex);
			m_async_copy_requests.push_back({buffer, ranges});
		}
		
		inline static void exec_memcpyRequests()
		{
			std::vector<std::future<void>> futureNodes(m_async_copy_requests.size());
			for (uint32_t i = 0; i < m_async_copy_requests.size(); i++)
				futureNodes[i] = std::async(
						std::launch::async, std::bind(&CopyRequest::exec_mem_copy, m_async_copy_requests[i])
				);
			
			for (auto& f : futureNodes)
				f.get();
			
			m_async_copy_requests.clear();
		}
	};
}