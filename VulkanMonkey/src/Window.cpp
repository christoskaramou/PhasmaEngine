#include "../include/Window.h"
#include "../include/Errors.h"
#include "../include/Timer.h"
#include <iostream>

using namespace vm;

constexpr auto MAX_WINDOWS = 20;

std::vector<std::unique_ptr<Renderer>> Window::renderer = {};
int Window::width = 0, Window::height = 0;

Window::Window() {}

Window::~Window() {}

void Window::create(const char* title, int w, int h, Uint32 flags)
{
	if (Window::renderer.size() != MAX_WINDOWS)
		Window::renderer.reserve(MAX_WINDOWS);

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
    if (Window::renderer.size() >= MAX_WINDOWS){
        std::cout << "Max windows number has been exceeded, define more (#define MAX_WINDOWS)\n";
        exit(-1);
    }
	SDL_Window* window;
    if (!((window = SDL_CreateWindow(title, x, y /*SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED*/, w, h, flags)))) { std::cout << SDL_GetError(); return; }
    std::cout << "Success at window creation\n";

	Window::renderer.push_back(std::make_unique<Renderer>(window));

    std::cout << std::endl;
    x += w + 12;
    if (x + w >= dm.w)
    {
        x = 12;
        y += h + 50;
    }
}

void Window::destroyAll()
{
	while (!renderer.empty()) {
		SDL_DestroyWindow(renderer.back()->ctx.vulkan.window);
		std::cout << "Window destroyed\n";
		renderer.pop_back();
	}
	SDL_Quit();
}

bool Window::processEvents(float delta)
{
	if (!renderer[0]->prepared) return false;
	if (Console::close_app) return false;
	static bool up = false;
	static bool down = false;
	static bool left = false;
	static bool right = false;
	static int xMove = 0;
	static int yMove = 0;

	auto& info = renderer[0]->ctx;
	bool combineDirections = false;

	ImGuiIO& io = ImGui::GetIO();

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_QUIT) return false;

		if (event.type == SDL_MOUSEWHEEL) {
			if (event.wheel.x > 0) io.MouseWheelH += 1;
			if (event.wheel.x < 0) io.MouseWheelH -= 1;
			if (event.wheel.y > 0) io.MouseWheel += 1;
			if (event.wheel.y < 0) io.MouseWheel -= 1;
		}

		if (event.type == SDL_MOUSEBUTTONDOWN) {
			if (event.button.button == SDL_BUTTON_LEFT) GUI::g_MousePressed[0] = true;
			if (event.button.button == SDL_BUTTON_RIGHT) GUI::g_MousePressed[1] = true;
			if (event.button.button == SDL_BUTTON_MIDDLE) GUI::g_MousePressed[2] = true;
		}

		if (event.type == SDL_TEXTINPUT)
			io.AddInputCharactersUTF8(event.text.text);

		if (event.type == SDL_KEYDOWN) {
			int key = event.key.keysym.scancode;
			IM_ASSERT(key >= 0 && key < IM_ARRAYSIZE(io.KeysDown));
			io.KeysDown[key] = true;
			if (event.key.keysym.sym == SDLK_ESCAPE) return false;
			if (event.key.keysym.sym == SDLK_w) up = true;
			if (event.key.keysym.sym == SDLK_s) down = true;
			if (event.key.keysym.sym == SDLK_a) left = true;
			if (event.key.keysym.sym == SDLK_d) right = true;
		}
		else if (event.type == SDL_KEYUP) {
			int key = event.key.keysym.scancode;
			IM_ASSERT(key >= 0 && key < IM_ARRAYSIZE(io.KeysDown));
			io.KeysDown[key] = false;
			io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
			io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
			io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
			io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);

			if (event.key.keysym.sym == SDLK_w) up = false;
			if (event.key.keysym.sym == SDLK_s) down = false;
			if (event.key.keysym.sym == SDLK_a) left = false;
			if (event.key.keysym.sym == SDLK_d) right = false;
		}
		if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
			if (isInsideRenderWindow(event.motion.x, event.motion.y)) {
				SDL_ShowCursor(SDL_DISABLE);
				SDL_WarpMouseInWindow(info.vulkan.window, static_cast<int>(GUI::winSize.x * .5f + GUI::winPos.x), static_cast<int>(GUI::winSize.y * .5f + GUI::winPos.y));
				if (event.type == SDL_MOUSEMOTION) {
					xMove = event.motion.x - static_cast<int>(GUI::winSize.x * .5f + GUI::winPos.x);
					yMove = event.motion.y - static_cast<int>(GUI::winSize.y * .5f + GUI::winPos.y);
					info.mainCamera.rotate((float)xMove, (float)yMove);
				}
			}
		}
		else
			SDL_ShowCursor(SDL_ENABLE);
		if (event.type == SDL_WINDOWEVENT) {
			if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				int w, h;
				SDL_GetWindowSize(info.vulkan.window, &width, &height);
				SDL_GL_GetDrawableSize(info.vulkan.window, &w, &h);
				renderer[0]->ctx.resizeViewport(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
			}
		}
	}

	if ((up || down) && (left || right)) combineDirections = true;

	if (up) info.mainCamera.move(Camera::RelativeDirection::FORWARD, delta, combineDirections);
	if (down) info.mainCamera.move(Camera::RelativeDirection::BACKWARD, delta, combineDirections);
	if (left) info.mainCamera.move(Camera::RelativeDirection::LEFT, delta, combineDirections);
	if (right) info.mainCamera.move(Camera::RelativeDirection::RIGHT, delta, combineDirections);

	return true;
}

bool Window::isInsideRenderWindow(int32_t x, int32_t y)
{
	return x > GUI::winPos.x && y > GUI::winPos.y && x < GUI::winPos.x + GUI::winSize.x && y < GUI::winPos.y + GUI::winSize.y;
}
