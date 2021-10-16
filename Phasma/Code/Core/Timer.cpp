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

#include "PhasmaPch.h"
#include "Timer.h"
#include "Renderer/Vulkan/Vulkan.h"
#include <thread>

namespace pe
{
	Timer::Timer() noexcept
	{
		m_start = {};
	}
	
	void Timer::Start() noexcept
	{
		m_start = std::chrono::high_resolution_clock::now();
	}
	
	double Timer::Count() noexcept
	{
		const std::chrono::duration<double> t_duration = std::chrono::high_resolution_clock::now() - m_start;
		return t_duration.count();
	}
	
	FrameTimer::FrameTimer() : Timer()
	{
		system_delay = 0;
		m_duration = {};
		delta = 0.0f;
		time = 0.0f;
		timestamps.resize(22);
	}
	
	void FrameTimer::Delay(double seconds)
	{
		if (seconds <= 0.0f)
			return;

		const std::chrono::nanoseconds delay{ (static_cast<size_t>(NANO(seconds)) - system_delay) };
		
		static Timer timer;
		timer.Start();
		std::this_thread::sleep_for(delay);
		system_delay = static_cast<size_t>(NANO(timer.Count())) - delay.count();
	}
	
	
	void FrameTimer::Tick() noexcept
	{
		m_duration = std::chrono::high_resolution_clock::now() - m_start;
		delta = m_duration.count();
		time += delta;
	}

	GPUTimer::GPUTimer()
	{
		const auto gpuProps = VULKAN.gpu->getProperties();
		if (!gpuProps.limits.timestampComputeAndGraphics)
			throw std::runtime_error("Timestamps not supported");
		
		timestampPeriod = gpuProps.limits.timestampPeriod;
		
		vk::QueryPoolCreateInfo qpci;
		qpci.queryType = vk::QueryType::eTimestamp;
		qpci.queryCount = 2;
		
		queryPool = std::make_unique<vk::QueryPool>(VULKAN.device->createQueryPool(qpci));
	}

	void GPUTimer::Reset()
	{
		_cmd->resetQueryPool(*queryPool, 0, 2);
	}
	
	void GPUTimer::Start(const vk::CommandBuffer* cmd)
	{
		_cmd = cmd;
		Reset();
		_cmd->writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *queryPool, 0);
	}

	float GPUTimer::End()
	{
		_cmd->writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *queryPool, 1);
		return GetTime();
	}
	
	float GPUTimer::GetTime()
	{
		const auto res = VULKAN.device->getQueryPoolResults(
			*queryPool, 0, 2, 2 * sizeof(uint64_t), &queries, sizeof(uint64_t),
			vk::QueryResultFlagBits::e64);

		if (res != vk::Result::eSuccess)
			return 0.f;

		return static_cast<float>(queries[1] - queries[0]) * timestampPeriod * 1e-6f;
	}
	
	void GPUTimer::Destroy() const noexcept
	{
		VULKAN.device->destroyQueryPool(*queryPool);
	}
}
