#include "Window.h"
#include "../Event/Event.h"
#include "../Console/Console.h"
#include <iostream>

using namespace vm;

std::vector<std::unique_ptr<Renderer>> Window::renderer{};

Window::Window() {}

Window::~Window() {}

void Window::create(const std::string& title, Uint32 flags) // flags = SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) { std::cout << SDL_GetError(); return; }

	SDL_Window* window;
	if (!((window = SDL_CreateWindow(title.c_str(), 50, 50, 800, 600, flags)))) { std::cout << SDL_GetError(); return; }

	renderer.push_back(std::make_unique<Renderer>(window));

	auto vulkan = VulkanContext::get();

	const std::string _title = 
		"VulkanMonkey3D   "
		+ std::string(vulkan->gpuProperties.deviceName)
		+ " (Present Mode: "
		+ vk::to_string(vulkan->surface->presentModeKHR)
		+ ")";

	SDL_SetWindowTitle(window, _title.c_str());
}

void Window::destroyAll()
{
	while (!renderer.empty()) {
		auto vulkan = VulkanContext::get();
		SDL_DestroyWindow(vulkan->window);
		renderer.pop_back();
	}
	SDL_Quit();
}

bool Window::processEvents(double delta)
{
	if (Console::close_app) return false;
	static int xMove = 0;
	static int yMove = 0;

	auto& info = renderer[0]->ctx;
	auto vulkan = VulkanContext::get();
	bool combineDirections = false;

	ImGuiIO& io = ImGui::GetIO();

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_QUIT) {
			FIRE_EVENT(Event::OnExit);
			return false;
		}

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
			const int key = event.key.keysym.scancode;
			io.KeysDown[key] = true;
		}
		else if (event.type == SDL_KEYUP) {
			const int key = event.key.keysym.scancode;
			io.KeysDown[key] = false;
			io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
			io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
			io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
			io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
		}
		if (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
			if (isInsideRenderWindow(event.motion.x, event.motion.y)) {
				SDL_ShowCursor(SDL_DISABLE);
				SDL_WarpMouseInWindow(vulkan->window, static_cast<int>(GUI::winSize.x * .5f + GUI::winPos.x), static_cast<int>(GUI::winSize.y * .5f + GUI::winPos.y));
				if (event.type == SDL_MOUSEMOTION) {
					xMove = event.motion.x - static_cast<int>(GUI::winSize.x * .5f + GUI::winPos.x);
					yMove = event.motion.y - static_cast<int>(GUI::winSize.y * .5f + GUI::winPos.y);
					info.camera_main.rotate(static_cast<float>(xMove), static_cast<float>(yMove));
				}
			}
		}
		else {
			SDL_ShowCursor(SDL_ENABLE);
		}
		if (event.type == SDL_WINDOWEVENT || event.type == GUI::scaleRenderTargetsEventType) {
			if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.type == GUI::scaleRenderTargetsEventType) {
				int w, h;
				SDL_GL_GetDrawableSize(vulkan->window, &w, &h);
				renderer[0]->ctx.resizeViewport(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
			}
		}
		if (event.type == GUI::compileShadersEventType) {
			renderer[0]->ctx.recreatePipelines();
		}
	}

	if (io.KeysDown[SDL_SCANCODE_ESCAPE]) {
		const SDL_MessageBoxButtonData buttons[] = { { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "cancel" }, { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "yes" } };
		const SDL_MessageBoxColorScheme colorScheme = { {{ 255,   0,   0 }, {   0, 255,   0 }, { 255, 255,   0 }, {   0,   0, 255 }, { 255,   0, 255 }} };
		const SDL_MessageBoxData messageboxdata = { SDL_MESSAGEBOX_INFORMATION, nullptr, "Exit", "Are you sure you want to exit?", SDL_arraysize(buttons), buttons, &colorScheme };
		int buttonid;
		SDL_ShowMessageBox(&messageboxdata, &buttonid);
		if (buttonid == 1)
			return false;
	}
	if ((io.KeysDown[SDL_SCANCODE_W] || io.KeysDown[SDL_SCANCODE_S]) &&
		(io.KeysDown[SDL_SCANCODE_A] || io.KeysDown[SDL_SCANCODE_D]))
		combineDirections = true;
	const float velocity = combineDirections ? GUI::cameraSpeed * static_cast<float>(delta) * 0.707f : GUI::cameraSpeed * static_cast<float>(delta);
	if (io.KeysDown[SDL_SCANCODE_W]) info.camera_main.move(Camera::RelativeDirection::FORWARD, velocity);
	if (io.KeysDown[SDL_SCANCODE_S]) info.camera_main.move(Camera::RelativeDirection::BACKWARD, velocity);
	if (io.KeysDown[SDL_SCANCODE_A]) info.camera_main.move(Camera::RelativeDirection::LEFT, velocity);
	if (io.KeysDown[SDL_SCANCODE_D]) info.camera_main.move(Camera::RelativeDirection::RIGHT, velocity);

	return true;
}

bool Window::isInsideRenderWindow(int32_t x, int32_t y)
{
	return x > GUI::winPos.x && y > GUI::winPos.y && x < GUI::winPos.x + GUI::winSize.x && y < GUI::winPos.y + GUI::winSize.y;
}