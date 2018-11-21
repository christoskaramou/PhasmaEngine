#include "include/Window.h"
#include "include/Timer.h"
#include <iostream>
#include <cstring>
#include <string>

using namespace vm;

constexpr int WIDTH = 1664;
constexpr int HEIGHT = 960;

int main(int argc, char* argv[])
{
	Window::create("", WIDTH, HEIGHT);
	auto& renderer = Window::renderer;
	auto& ctx = renderer[0]->ctx; // main renderer info

    std::string title = "VulkanMonkey3D   " + std::string(ctx.vulkan.gpuProperties.deviceName) + " (Present Mode: " + vk::to_string(ctx.vulkan.surface->presentModeKHR) + ")  -  FPS: ";
    SDL_SetWindowTitle(ctx.vulkan.window, title.c_str());

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
            SDL_SetWindowTitle(ctx.vulkan.window, _fps.c_str());
        }
    }

	Window::destroyAll();

    return 0;
}