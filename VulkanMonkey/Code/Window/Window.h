#pragma once
#include "../Renderer/Renderer.h"

namespace vm
{
	class Context;

	class Window
	{
	public:
		SDL_Window* Create(Context* ctx, const std::string& title = "", uint32_t flags = SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
		void DestroyAll();
		bool ProcessEvents(double delta);
		bool isInsideRenderWindow(int32_t x, int32_t y);
		bool isMinimized();

	private:
		Context* ctx = nullptr;
	};
}