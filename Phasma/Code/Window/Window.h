#pragma once

#include "../Renderer/Renderer.h"

namespace pe
{
	class Context;

	class Window
	{
	public:
		SDL_Window* Create(
				Context* ctx,
				uint32_t flags = SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN
		);

		void DestroyAll();

		bool ProcessEvents(double delta);

		bool isInsideRenderWindow(int32_t x, int32_t y);

		bool isMinimized();

		void SetTitle(const std::string& title);

		SDL_Window* Handle()
		{ return handle; }

	private:
		Context* ctx = nullptr;
		SDL_Window* handle = nullptr;
	};
}