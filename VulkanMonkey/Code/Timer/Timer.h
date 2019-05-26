#pragma once
#include <chrono>
#include <vector>
#include "../../include/SDL.h"

namespace vm {
	class Timer
	{
	public:
		Timer();
		~Timer();

		float getDelta(float timeScale = 1.0f);
		unsigned getFPS();
		bool intervalsOf(float seconds);
		void minFrameTime(float seconds);
		static float getTotalTime();
		static float delta;
		static std::chrono::high_resolution_clock::time_point frameStart;
		static float noWaitDelta;
	private:
		std::chrono::high_resolution_clock::time_point start;
		std::chrono::duration<float> duration;
		float _minFrameTime;
	protected:
		static unsigned totalCounter;
		static float totalTime;
		static float time;
		static unsigned instances;
		static std::vector<float> deltas;
	};
}