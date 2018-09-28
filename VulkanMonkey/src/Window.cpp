#include "../include/Window.h"
#include "../include/Errors.h"
#include "../include/Timer.h"
#include <iostream>

constexpr auto MAX_WINDOWS = 100;

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
		SDL_DestroyWindow(renderer.back()->info.window);
		std::cout << "Window destroyed\n";
		renderer.pop_back();
	}
	SDL_Quit();
}

bool Window::processEvents(float delta)
{
	if (!renderer[0]->prepared) return false;
	static bool up = false;
	static bool down = false;
	static bool left = false;
	static bool right = false;
	static int xMove = 0;
	static int yMove = 0;

	auto& info = renderer[0]->info;
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
			if (event.key.keysym.sym == SDLK_UP) info.light[0].position.y += 0.005f;
			if (event.key.keysym.sym == SDLK_DOWN) info.light[0].position.y -= 0.005f;
			if (event.key.keysym.sym == SDLK_LEFT) info.light[0].position.z += 0.01f;
			if (event.key.keysym.sym == SDLK_RIGHT) info.light[0].position.z -= 0.01f;
			//std::cout << info.light[0].position.x << ", " << info.light[0].position.y << ", " << info.light[0].position.z << "\n";
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
			if (event.key.keysym.sym == SDLK_1) Shadows::shadowCast = !Shadows::shadowCast;
			if (event.key.keysym.sym == SDLK_2) {
				for (uint32_t i = 1; i < info.light.size(); i++) {
					info.light[i] = { glm::vec4(renderer[0]->rand(0.f,1.f), renderer[0]->rand(0.0f,1.f), renderer[0]->rand(0.f,1.f), renderer[0]->rand(0.f,1.f)),
						glm::vec4(renderer[0]->rand(-1.f,1.f), renderer[0]->rand(.3f,1.f), renderer[0]->rand(-3.5f,.5f), 1.f),
						glm::vec4(10.f, 1.f, 1.f, 1.f),
						glm::vec4(0.f, 0.f, 0.f, 1.f) };
				}
				memcpy(info.uniformBufferLights.data, info.light.data(), info.light.size() * sizeof(Light));
			}
			if (event.key.keysym.sym == SDLK_3) for (auto& model : info.models) model.render = !info.models[0].render;
			if (event.key.keysym.sym == SDLK_4) info.gui.render = !info.gui.render;
			if (event.key.keysym.sym == SDLK_5) info.terrain.render = !info.terrain.render;
			if (event.key.keysym.sym == SDLK_6) info.skyBox.render = !info.skyBox.render;
			if (event.key.keysym.sym == SDLK_0) for (auto& r : renderer) r->overloadedGPU = !renderer[0]->overloadedGPU;
		}
		if (event.type == SDL_MOUSEWHEEL) {
			info.mainCamera.speed *= event.wheel.y > 0 ? 1.2f : 0.833f;
		}
		if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
			SDL_ShowCursor(SDL_DISABLE);
			SDL_WarpMouseInWindow(info.window, static_cast<int>(renderer[0]->info.surface.actualExtent.width * .5f), static_cast<int>(renderer[0]->info.surface.actualExtent.height * .5f));
			if (event.type == SDL_MOUSEMOTION) {
				xMove = -(event.motion.x - static_cast<int>(renderer[0]->info.surface.actualExtent.width * .5f));
				yMove = -(event.motion.y - static_cast<int>(renderer[0]->info.surface.actualExtent.height * .5f));
				info.mainCamera.rotate((float)xMove, (float)yMove);
			}
		}
		else
			SDL_ShowCursor(SDL_ENABLE);
		if (event.type == SDL_WINDOWEVENT) {
			if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				renderer[0]->resizeViewport();
		}
	}

	if ((up || down) && (left || right)) combineDirections = true;

	if (up) info.mainCamera.move(Camera::RelativeDirection::FORWARD, delta, combineDirections);
	if (down) info.mainCamera.move(Camera::RelativeDirection::BACKWARD, delta, combineDirections);
	if (left) info.mainCamera.move(Camera::RelativeDirection::LEFT, delta, combineDirections);
	if (right) info.mainCamera.move(Camera::RelativeDirection::RIGHT, delta, combineDirections);

	return true;
}