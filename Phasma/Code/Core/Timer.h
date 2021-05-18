#pragma once

#include <chrono>
#include <vector>
#include <memory>

#define Arithmetic_Template(T1, T2) \
template < \
    typename T1, \
    typename = typename std::enable_if<std::is_arithmetic<T1>::value, Return_T>::type, \
    typename T2, \
    typename = typename std::enable_if<std::is_arithmetic<T2>::value, Param_T>::type>

Arithmetic_Template(Return_T, Param_T) constexpr Return_T SECONDS_TO_MILLISECONDS(Param_T seconds)
{ return static_cast<Return_T>(seconds * static_cast<Param_T>(1000)); }

Arithmetic_Template(Return_T, Param_T) constexpr Return_T SECONDS_TO_MICROSECONDS(Param_T seconds)
{ return static_cast<Return_T>(seconds * static_cast<Param_T>(1000000)); }

Arithmetic_Template(Return_T, Param_T) constexpr Return_T SECONDS_TO_NANOSECONDS(Param_T seconds)
{ return static_cast<Return_T>(seconds * static_cast<Param_T>(1000000000)); }

namespace pe
{
	class Timer
	{
	public:
		Timer() noexcept;

		void Start() noexcept;

		double Count() noexcept;

	protected:
		std::chrono::high_resolution_clock::time_point m_start;
	};

	class FrameTimer : public Timer
	{
	public:
		void Tick() noexcept;

		void Delay(double seconds = 0.0f);

		double delta;
		double time;
		std::vector<double> timestamps {};
	private:
		Timer timer;
		size_t system_delay;
		std::chrono::duration<double> m_duration {};

	public:
		static auto& Instance() noexcept
		{
			static FrameTimer frame_timer;
			return frame_timer;
		}

		FrameTimer(FrameTimer const&) = delete;                // copy constructor
		FrameTimer(FrameTimer&&) noexcept = delete;            // move constructor
		FrameTimer& operator=(FrameTimer const&) = delete;    // copy assignment
		FrameTimer& operator=(FrameTimer&&) = delete;        // move assignment
	private:
		~FrameTimer() = default;                            // destructor
		FrameTimer();                                        // default constructor
	};
}

namespace vk
{
	class CommandBuffer;
	class QueryPool;
}
namespace pe
{
	class GPUTimer
	{
	public:
		GPUTimer();

		void start(const vk::CommandBuffer* cmd) noexcept;

		void end(float* res = nullptr);

		float getTime();

		void destroy() const noexcept;

	private:
		std::unique_ptr<vk::QueryPool> queryPool;
		std::vector<uint64_t> queryTimes {};
		float timestampPeriod;
		const vk::CommandBuffer* _cmd;
	};
}
