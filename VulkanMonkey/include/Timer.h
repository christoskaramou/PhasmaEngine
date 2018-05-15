#ifndef TIMER_H
#define TIMER_H
#include <chrono>

class Timer
{
    public:
        Timer();
        ~Timer();
        static unsigned wantedFPS;
		static unsigned FPS;
        static unsigned counter;
        static float totalTime;
        static float delta;
        static float time;
        static unsigned instances;
        float getDelta();
        bool timePassed(float time);
    private:
        std::chrono::high_resolution_clock::time_point start;
        std::chrono::duration<float> duration;
};

#endif // TIMER_H
