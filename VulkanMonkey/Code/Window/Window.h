#pragma once
#include "../Renderer/Renderer.h"

namespace vm
{
	class Window
	{
	public:
		static void create(const std::string& title = "", Uint32 flags = SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
		static void destroyAll();
		static bool processEvents(double delta);
		static bool isInsideRenderWindow(int32_t x, int32_t y);
		static bool isMinimized();

		static std::vector<std::unique_ptr<Renderer>> renderer; // window pointer is also stored in here
	private:
		Window();
		~Window();
	};
}