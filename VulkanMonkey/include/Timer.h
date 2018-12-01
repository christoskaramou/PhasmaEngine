#pragma once
#include <chrono>
#include <vector>
#include "SDL.h"

namespace vm {
	class Timer
	{
	public:
		Timer();
		~Timer();

		float getDelta();
		unsigned getFPS();
		bool intervalsOf(float seconds);
		void minFrameTime(float seconds);
		static float delta;
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