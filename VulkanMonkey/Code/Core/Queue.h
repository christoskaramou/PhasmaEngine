#pragma once
#include <string>
#include <future>
#include <tuple>
#include <deque>
#include <any>
#include <mutex>
#include <map>
#include "Timer.h"
#include "Buffer.h"
#include "../MemoryHash/MemoryHash.h"
#include "../VulkanContext/VulkanContext.h"
#include <iostream>

namespace vm
{
	class CopyRequest
	{
	public:
		Buffer* buffer;
		std::vector<MemoryRange> memory_ranges{};

		void exec_mem_copy()
		{
			buffer->map();
			for (auto& memory_range : memory_ranges)
				buffer->copyData(memory_range.data, memory_range.size, memory_range.offset);
			buffer->flush();
			buffer->unmap();
		}
	};

	struct DescriptorCache
	{
		vk::DescriptorSetLayout* descriptorSetLayout;
		std::vector<vk::DescriptorSet*> descriptorSets;
		std::vector<vk::WriteDescriptorSet> textureWriteSets;
		std::vector<vk::DescriptorBufferInfo> descriptorBufferInfos;
		std::vector<vk::DescriptorImageInfo> descriptorImageInfos;
	};

	class Queue
	{
	public:
		// std::deque insertion and deletion at either end of a deque never invalidates pointers or references to the rest of the elements
		inline static std::deque<std::tuple<std::string, std::string>> loadModel{};
		inline static std::deque<int> unloadModel{};
		inline static std::deque<std::tuple<int, std::string>> addScript{};
		inline static std::deque<int> removeScript{};
		inline static std::deque<int> compileScript{};
		inline static std::deque<std::future<std::any>> loadModelFutures{};
	private:
		inline static std::unordered_map<size_t, DescriptorCache> m_descriptorCaches{};
		inline static std::vector<CopyRequest> m_async_copy_requests{};
		inline static std::mutex m_mem_cpy_request_mutex{};
		inline static std::mutex m_descriptor_cache_mutex{};
	public:
		inline static DescriptorCache* getDescriptorCache(size_t hash)
		{
			if (m_descriptorCaches.find(hash) != m_descriptorCaches.end())
			{
				return &m_descriptorCaches[hash];
			}
			return nullptr;
		}
		inline static size_t addDescriptorCache(const DescriptorCache& descriptorCache)
		{
			std::lock_guard<std::mutex> guard(m_descriptor_cache_mutex);

			const size_t hash = MemoryHash(descriptorCache).getHash();
			if (m_descriptorCaches.find(hash) == m_descriptorCaches.end())
			{
				m_descriptorCaches[hash] = descriptorCache;
			}
			return hash;
		}
		inline static void memcpyRequest(Buffer* buffer, const std::vector<MemoryRange>& ranges)
		{
			std::lock_guard<std::mutex> guard(m_mem_cpy_request_mutex);
			m_async_copy_requests.push_back({ buffer, ranges });
		}
		inline static void exec_memcpyRequests(uint32_t previousImageIndex)
		{
			static Timer timer;
			timer.Start();
			VulkanContext::get()->waitFences(VulkanContext::get()->fences[previousImageIndex]);
			FrameTimer::Instance().timestamps[0] = timer.Count();

			std::vector<std::future<void>> futureNodes(m_async_copy_requests.size());
			for (uint32_t i = 0; i < m_async_copy_requests.size(); i++)
				futureNodes[i] = std::async(std::launch::async, std::bind(&CopyRequest::exec_mem_copy, m_async_copy_requests[i]));

			for (auto& f : futureNodes)
				f.get();

			m_async_copy_requests.clear();
		}
	};
}