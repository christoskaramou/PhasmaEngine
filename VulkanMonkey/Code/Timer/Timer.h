#pragma once

#include <chrono>
#include <vector>
#include <memory>

#define SECONDS_TO_MILLISECONDS(seconds) seconds * 1000
#define SECONDS_TO_MICROSECONDS(seconds) seconds * 1000000
#define SECONDS_TO_NANOSECONDS(seconds) seconds * 1000000000

namespace vm
{
	struct Timer
	{
	public:
		Timer() noexcept;

		void Start() noexcept;
		double Count() noexcept;
	protected:
		std::chrono::high_resolution_clock::time_point m_start;
	};

	struct FrameTimer : Timer
	{
	public:
		void Tick() noexcept;
		void Delay(double seconds = 0.0f);

		double delta;
		double time;
		std::vector<double> timestamps{};
	private:
		Timer timer;
		size_t system_delay;
		std::chrono::duration<double> m_duration{};

	public:
		static auto& Instance() noexcept { static FrameTimer frame_timer; return frame_timer; }
		FrameTimer(FrameTimer const&) = delete;				// copy constructor
		FrameTimer(FrameTimer&&) noexcept = delete;			// move constructor
		FrameTimer& operator=(FrameTimer const&) = delete;	// copy assignment
		FrameTimer& operator=(FrameTimer&&) = delete;		// move assignment
	private:
		~FrameTimer() = default;							// destructor
		FrameTimer();										// default constructor
	};
}

namespace vk
{
	class CommandBuffer;
	class QueryPool;
}
namespace vm
{
	struct GPUTimer
	{
		GPUTimer();
		void start(const vk::CommandBuffer* cmd) noexcept;
		void end(float* res = nullptr);
		float getTime();
		void destroy() const noexcept;

	private:
		std::unique_ptr<vk::QueryPool> queryPool;
		std::vector<uint64_t> queryTimes{};
		float timestampPeriod;
		const vk::CommandBuffer* _cmd;
	};
}
