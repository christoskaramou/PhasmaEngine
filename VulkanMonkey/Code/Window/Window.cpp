#include "Window.h"
#include <iostream>

using namespace vm;

std::vector<std::unique_ptr<Renderer>> Window::renderer = {};

Window::Window() {}

Window::~Window() {}

void Window::create(std::string title, Uint32 flags) // flags = SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) { std::cout << SDL_GetError(); return; }

	SDL_Window* window;
	if (!((window = SDL_CreateWindow(title.c_str(), 50, 50, 800, 600, flags)))) { std::cout << SDL_GetError(); return; }

	Window::renderer.push_back(std::make_unique<Renderer>(window));

	std::string _title = 
		"VulkanMonkey3D   "
		+ std::string(Window::renderer.back()->ctx.vulkan.gpuProperties.deviceName)
		+ " (Present Mode: "
		+ vk::to_string(Window::renderer.back()->ctx.vulkan.surface->presentModeKHR)
		+ ")";

	SDL_SetWindowTitle(window, _title.c_str());
}

void Window::destroyAll()
{
	while (!renderer.empty()) {
		SDL_DestroyWindow(renderer.back()->ctx.vulkan.window);
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
	static float lastTimeScale = GUI::timeScale;

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
			if (event.key.keysym.sym == SDLK_p) { if (GUI::timeScale) lastTimeScale = GUI::timeScale; GUI::timeScale = 0.0f; }
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
			if (event.key.keysym.sym == SDLK_p) GUI::timeScale = lastTimeScale;
		}
		if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
			if (isInsideRenderWindow(event.motion.x, event.motion.y)) {
				SDL_ShowCursor(SDL_DISABLE);
				SDL_WarpMouseInWindow(info.vulkan.window, static_cast<int>(GUI::winSize.x * .5f + GUI::winPos.x), static_cast<int>(GUI::winSize.y * .5f + GUI::winPos.y));
				if (event.type == SDL_MOUSEMOTION) {
					xMove = event.motion.x - static_cast<int>(GUI::winSize.x * .5f + GUI::winPos.x);
					yMove = event.motion.y - static_cast<int>(GUI::winSize.y * .5f + GUI::winPos.y);
					info.camera_main.rotate((float)xMove, (float)yMove);
				}
			}
		}
		else
			SDL_ShowCursor(SDL_ENABLE);
		if (event.type == SDL_WINDOWEVENT) {
			if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				int w, h;
				SDL_GL_GetDrawableSize(info.vulkan.window, &w, &h);
				renderer[0]->ctx.resizeViewport(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
			}
		}
	}

	if ((up || down) && (left || right)) combineDirections = true;
	float velocity = combineDirections ? GUI::cameraSpeed * Timer::delta * 0.707f : GUI::cameraSpeed * Timer::delta;
	if (up) info.camera_main.move(Camera::RelativeDirection::FORWARD, velocity);
	if (down) info.camera_main.move(Camera::RelativeDirection::BACKWARD, velocity);
	if (left) info.camera_main.move(Camera::RelativeDirection::LEFT, velocity);
	if (right) info.camera_main.move(Camera::RelativeDirection::RIGHT, velocity);

	return true;
}

bool Window::isInsideRenderWindow(int32_t x, int32_t y)
{
	return x > GUI::winPos.x && y > GUI::winPos.y && x < GUI::winPos.x + GUI::winSize.x && y < GUI::winPos.y + GUI::winSize.y;
}
