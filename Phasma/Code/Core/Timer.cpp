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
#include "../Renderer/RenderApi.h"
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
		timestamps.resize(1);
	}
	
	void FrameTimer::Delay(double seconds)
	{
		const std::chrono::nanoseconds delay {SECONDS_TO_NANOSECONDS<size_t>(seconds) - system_delay};
		
		timer.Start();
		std::this_thread::sleep_for(delay);
		system_delay = SECONDS_TO_NANOSECONDS<size_t>(timer.Count()) - delay.count();
	}
	
	
	void FrameTimer::Tick() noexcept
	{
		m_duration = std::chrono::high_resolution_clock::now() - m_start;
		delta = m_duration.count();
		time += delta;
	}
	
	GPUTimer::GPUTimer()
	{
		const auto gpuProps = VulkanContext::Get()->gpu->getProperties();
		if (!gpuProps.limits.timestampComputeAndGraphics)
			throw std::runtime_error("Timestamps not supported");
		
		timestampPeriod = gpuProps.limits.timestampPeriod;
		
		vk::QueryPoolCreateInfo qpci;
		qpci.queryType = vk::QueryType::eTimestamp;
		qpci.queryCount = 2;
		
		queryPool = std::make_unique<vk::QueryPool>(VulkanContext::Get()->device->createQueryPool(qpci));
		
		queryTimes.resize(2, 0);
	}
	
	void GPUTimer::start(const vk::CommandBuffer* cmd) noexcept
	{
		_cmd = cmd;
		_cmd->resetQueryPool(*queryPool, 0, 2);
		_cmd->writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *queryPool, 0);
	}
	
	void GPUTimer::end(float* res)
	{
		_cmd->writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *queryPool, 1);
		if (res)
			*res = getTime();
	}
	
	float GPUTimer::getTime()
	{
		const auto res = VulkanContext::Get()->device->getQueryPoolResults(
				*queryPool, 0, 2, sizeof(uint64_t) * queryTimes.size(), queryTimes.data(), sizeof(uint64_t),
				vk::QueryResultFlagBits::e64
		);
		if (res != vk::Result::eSuccess)
			return 0.0f;
		return static_cast<float>(queryTimes[1] - queryTimes[0]) * timestampPeriod * 1e-6f;
	}
	
	void GPUTimer::destroy() const noexcept
	{
		VulkanContext::Get()->device->destroyQueryPool(*queryPool);
	}
}
