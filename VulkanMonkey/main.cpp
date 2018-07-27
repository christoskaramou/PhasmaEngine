#include "include/Window.h"
#include "include/Timer.h"
#include <iostream>
#include <cstring>
#include <string>

const int WIDTH = 1680;
const int HEIGHT = 1050;

int main(int argc, char* argv[])
{
    auto window = std::make_unique<Window>();
    window->create("", WIDTH, HEIGHT);
	auto& renderer = window->renderer;
	auto& info = renderer[0]->info; // main renderer info

    std::string deviceName =info.gpuProperties.deviceName;
    std::string title = "VulkanMonkey3D   " + deviceName + " (Present Mode: " + vk::to_string(info.surface.presentModeKHR) + ")  -  FPS: ";
    SDL_SetWindowTitle(info.window, title.c_str());

    Timer::wantedFPS = 0;
    while(window->processEvents(Timer::delta))
	{
        Timer timer;
		for (unsigned i = 0; i < renderer.size(); i++) {
			
			renderer[i]->update(timer.delta);
			renderer[i]->draw();
        }

        if (timer.timePassed(1.f))
        {
            std::string _fps = title + std::to_string(timer.FPS);
            SDL_SetWindowTitle(info.window, _fps.c_str());
        }
    }
    return 0;
}
