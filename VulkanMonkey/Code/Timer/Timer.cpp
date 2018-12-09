#include "Timer.h"
#include <iostream>

using namespace vm;

unsigned			Timer::totalCounter = 0;
float				Timer::totalTime = 0.0f;
float				Timer::delta = 0.0f;
float				Timer::time = 0.0f;
unsigned			Timer::instances = 0;
std::vector<float>	Timer::deltas(20, 0.f);

Timer::Timer()
{
	_minFrameTime = 0.0f;
    if (++instances > 1)
    {
        exit(-1);
    }
    start = std::chrono::high_resolution_clock::now();
}

Timer::~Timer()
{
    if (_minFrameTime > 0){
        duration = std::chrono::high_resolution_clock::now() - start;
		float delay = _minFrameTime - duration.count();
		if (delay > 0.f)
			SDL_Delay(static_cast<unsigned>(delay * 1000.f)); // not accurate but fast and not CPU cycle consuming
    }

    duration = std::chrono::high_resolution_clock::now() - start;
	delta = duration.count();

	deltas[totalCounter % 20] = delta;
    time += delta; // for any time rate given
    totalTime += delta;
    totalCounter++;
    instances--;
}

float Timer::getDelta()
{
    return delta;
}

unsigned Timer::getFPS()
{
	float sum = 0.f;
	for (auto &d : deltas) sum += d;
	return static_cast<unsigned int>(deltas.size() / sum);
}

bool Timer::intervalsOf(float seconds)
{
    if (time > seconds)
    {
        time = 0.0f;
        return true;
    }
	else
		return false;
}

void Timer::minFrameTime(float seconds)
{
	_minFrameTime = seconds;
}