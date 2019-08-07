#pragma once
#include <chrono>
#include <vector>

namespace vm {
	class Timer
	{
	public:
		Timer();
		~Timer();

		static float getDelta(float timeScale = 1.0f);
		static unsigned getFPS();
		static bool intervalsOf(float seconds);
		void minFrameTime(float seconds);
		static float getTotalTime();
		static float delta;
		static float cleanDelta; // delta with no delay for taget FPS
		static float waitingTime;
		static std::chrono::high_resolution_clock::time_point frameStart;
	private:
		std::chrono::high_resolution_clock::time_point start;
		std::chrono::duration<float> duration{};
		float _minFrameTime;
	protected:
		static unsigned totalCounter;
		static float totalTime;
		static float time;
		static unsigned instances;
		static std::vector<float> deltas;
	};
}
