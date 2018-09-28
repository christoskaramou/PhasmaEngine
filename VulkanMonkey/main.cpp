#include "include/Window.h"
#include "include/Timer.h"
#include <iostream>
#include <cstring>
#include <string>

constexpr int WIDTH = 1644;
constexpr int HEIGHT = 936;

int main(int argc, char* argv[])
{
	Window::create("", WIDTH, HEIGHT);
	auto& renderer = Window::renderer;
	auto& info = renderer[0]->info; // main renderer info

    //std::string deviceName = info.gpuProperties.deviceName;
    std::string title = "VulkanMonkey3D   " + std::string(info.gpuProperties.deviceName) + " (Present Mode: " + vk::to_string(info.surface.presentModeKHR) + ")  -  FPS: ";
    SDL_SetWindowTitle(info.window, title.c_str());

    while(true)
	{
        Timer timer;
		//timer.minFrameTime(0.0167f);

		if (!Window::processEvents(timer.getDelta()))
			break;

		for (unsigned i = 0; i < renderer.size(); i++) {
			renderer[i]->update(timer.getDelta());
			renderer[i]->present();
        }

        if (timer.intervalsOf(0.5f))
        {
			std::string _fps = title + std::to_string(timer.getFPS());
            SDL_SetWindowTitle(info.window, _fps.c_str());
        }
    }

	Window::destroyAll();

    return 0;
}