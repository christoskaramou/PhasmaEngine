#include "../include/Timer.h"
#include <iostream>

unsigned Timer::wantedFPS = 0;
unsigned Timer::FPS = 0;
unsigned Timer::counter = 0;
float Timer::totalTime = 0.0f;
float Timer::delta = 0.0f;
float Timer::time = 0.0f;
unsigned Timer::instances = 0;

Timer::Timer()
{
    if (instances++ > 0)
    {
        std::cout << "No more than one instance of Timer can run properly yet!\n";
        exit(-1);
    }
    start = std::chrono::high_resolution_clock::now();
}

Timer::~Timer()
{
    if (wantedFPS > 0){
        float spf = 1.0f/ wantedFPS;
        duration = std::chrono::high_resolution_clock::now() - start;
        while( duration.count() < spf )
            duration = std::chrono::high_resolution_clock::now() - start;
        delta = duration.count();
    }
    else{
        duration = std::chrono::high_resolution_clock::now() - start;
        delta = duration.count();
    }
    time += delta; // to check for FPS at any time rate given
    totalTime += delta;
    counter++;
    instances--;
}

float Timer::getDelta()
{
    return delta;
}

bool Timer::timePassed(float time)
{
    if (this->time > time)
    {
        this->time = 0.0f;
		FPS = (float) counter / totalTime;
		totalTime = 0.f;
		counter = 0;
        return true;
    }
    return false;
}
