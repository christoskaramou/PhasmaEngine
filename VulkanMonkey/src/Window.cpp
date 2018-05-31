#include "../include/Window.h"
#include "../include/Errors.h"
#include "../include/Timer.h"
#include <iostream>

#ifndef MAX_WINDOWS
#define MAX_WINDOWS 5
#endif // MAX_WINDOWS

Window::Window()
{
	// reserve to ensure a stable pointer to this vector
	renderer.reserve(MAX_WINDOWS);
}

Window::~Window()
{
	while (!renderer.empty()) {
		SDL_DestroyWindow(renderer.back()->info.window);
		std::cout << "Window destroyed\n";
		renderer.pop_back();
	}
    SDL_Quit();

    std::cout << "Avrg ms/frame: " << Timer::totalTime/(float)Timer::counter << std::endl;
}

void Window::create(const char* title, int w, int h, Uint32 flags)
{
	width = w;
	height = h;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) { std::cout << SDL_GetError(); return; }
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
        SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        exit(-1);
    }
    static int x = 12;
    static int y = 50;

    if (y + h >= dm.h){
        std::cout << "No more windows can fit in the " << dm.w << "x" << dm.h << " screen resolution.\nIgnoring...\n";
        return;
    }
    if (renderer.size() >= MAX_WINDOWS){
        std::cout << "Max windows number has been exceeded, define more (#define MAX_WINDOWS)\n";
        exit(-1);
    }
	SDL_Window* window;
    if (!(window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags))) { std::cout << SDL_GetError(); return; }
    std::cout << "Success at window creation\n";

    renderer.push_back(std::make_unique<Renderer>(window));

    std::cout << std::endl;
    x += w + 12;
    if (x + w >= dm.w)
    {
        x = 12;
        y += h + 50;
    }
}

bool Window::processEvents(float delta)
{
	static bool up = false;
	static bool down = false;
	static bool left = false;
	static bool right = false;
	static int xMove = 0;
	static int yMove = 0;

	auto& info = renderer[0]->info;
	bool combineDirections = false;

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_QUIT) return false;

		if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.sym == SDLK_ESCAPE) return false;
			if (event.key.keysym.sym == SDLK_w) up = true;
			if (event.key.keysym.sym == SDLK_s) down = true;
			if (event.key.keysym.sym == SDLK_a) left = true;
			if (event.key.keysym.sym == SDLK_d) right = true;
			if (event.key.keysym.sym == SDLK_UP) info.light[0].position.y += 0.005;
			if (event.key.keysym.sym == SDLK_DOWN) info.light[0].position.y -= 0.005;
			if (event.key.keysym.sym == SDLK_LEFT) info.light[0].position.z += 0.005;
			if (event.key.keysym.sym == SDLK_RIGHT) info.light[0].position.z -= 0.005;
		}
		else if (event.type == SDL_KEYUP) {
			if (event.key.keysym.sym == SDLK_w) up = false;
			if (event.key.keysym.sym == SDLK_s) down = false;
			if (event.key.keysym.sym == SDLK_a) left = false;
			if (event.key.keysym.sym == SDLK_d) right = false;
			if (event.key.keysym.sym == SDLK_1) Shadows::shadowCast = !Shadows::shadowCast;
			if (event.key.keysym.sym == SDLK_2) {
				for (uint32_t i = 1; i < info.light.size(); i++) {
					info.light[i] = { glm::vec4(renderer[0]->rand(0.f,1.f), renderer[0]->rand(0.0f,1.f), renderer[0]->rand(0.f,1.f), renderer[0]->rand(0.f,1.f)),
						glm::vec4(renderer[0]->rand(-1.f,1.f), renderer[0]->rand(.3f,1.f), renderer[0]->rand(-.5f,.5f), 1.f),
						glm::vec4(2.f, 1.f, 1.f, 1.f) };
				}
			}
		}
		if (event.type == SDL_MOUSEMOTION) {
			xMove -= event.motion.xrel;
			yMove -= event.motion.yrel;
			info.mainCamera.rotate((float)xMove, (float)yMove);
		}
		if (event.type == SDL_MOUSEWHEEL) {
			info.mainCamera.speed *= event.wheel.y > 0 ? 1.2f : 0.833f;
		}
	}
	if (up && left) combineDirections = true;
	if (up && right) combineDirections = true;
	if (down && left) combineDirections = true;
	if (down && right) combineDirections = true;

	if (up) info.mainCamera.move(Camera::RelativeDirection::FORWARD, delta, combineDirections);
	if (down) info.mainCamera.move(Camera::RelativeDirection::BACKWARD, delta, combineDirections);
	if (left) info.mainCamera.move(Camera::RelativeDirection::LEFT, delta, combineDirections);
	if (right) info.mainCamera.move(Camera::RelativeDirection::RIGHT, delta, combineDirections);

	SDL_WarpMouseInWindow(info.window, width / 2, height / 2);

	return true;
}
