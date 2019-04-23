#include "Timer.h"
#include <stdexcept>

using namespace vm;

unsigned			Timer::totalCounter = 0;
float				Timer::totalTime = 0.0f;
float				Timer::delta = 0.0f;
float				Timer::time = 0.0f;
unsigned			Timer::instances = 0;
std::vector<float>	Timer::deltas(20, 0.f);
std::chrono::high_resolution_clock::time_point Timer::frameStart = {};
float				Timer::noWaitDelta = 0.f;

Timer::Timer()
{
	_minFrameTime = 0.0f;
    if (++instances > 1)
		throw std::runtime_error("Only one active instance of timer is allowed");

    start = std::chrono::high_resolution_clock::now();
	frameStart = start;
	duration = {};
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

	deltas[totalCounter++] = delta;
    totalCounter %= deltas.size();
    time += delta; // for any time rate given
    totalTime += delta;
    instances--;
}

float Timer::getDelta(float timeScale)
{
    return delta * timeScale;
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