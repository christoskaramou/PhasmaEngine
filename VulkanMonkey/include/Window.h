#pragma once
#include "Renderer.h"

namespace vm {
	class Window
	{
	public:
		static void create(const char* title, int w, int h, Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
		static void destroyAll();
		static bool processEvents(float delta);
		static bool isInsideRenderWindow(int32_t x, int32_t y);

		static std::vector<std::unique_ptr<Renderer>> renderer; // window pointer is also stored in here
	private:
		Window();
		~Window();
		static int width, height;
	};
}