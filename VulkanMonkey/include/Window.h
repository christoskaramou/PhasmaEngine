#ifndef WINDOW_H
#define WINDOW_H

#include "Renderer.h"

class Window
{
    public:
        Window();
        ~Window();
        void create(const char* title, int w, int h, Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
		bool processEvents(float delta);

		std::vector<std::unique_ptr<Renderer>> renderer{}; // window pointer is also stored in here
	private:
		int width, height;
};

#endif // WINDOW_H
