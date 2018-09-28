#ifndef TIMER_H
#define TIMER_H
#include <chrono>
#include <vector>
#include "SDL/SDL.h"
class Timer
{
    public:
        Timer();
        ~Timer();

        float getDelta();
		unsigned getFPS();
        bool intervalsOf(float seconds);
		void minFrameTime(float seconds);
    private:
        std::chrono::high_resolution_clock::time_point start;
        std::chrono::duration<float> duration;
	protected:
		static float _minFrameTime;
		static unsigned totalCounter;
		static float totalTime;
		static float delta;
		static float time;
		static unsigned instances;
		static std::vector<float> deltas;
};

#endif // TIMER_H