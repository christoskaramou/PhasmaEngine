#include "PhasmaPch.h"
#include "Window.h"
#include "../Console/Console.h"
#include "../Renderer/RenderApi.h"
#include "../Renderer/Renderer.h"
#include "../ECS/Context.h"
#include "../Event/EventSystem.h"
#include <iostream>

namespace pe
{
	SDL_Window* Window::Create(Context* ctx, uint32_t flags)
	{
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
		{
			std::cout << SDL_GetError();
			return nullptr;
		}
		
		if (!((m_handle = SDL_CreateWindow("", 50, 50, 800, 600, flags))))
		{
			std::cout << SDL_GetError();
			return nullptr;
		}
		
		auto lambda = [this](const std::any& title)
		{ SetTitle(std::any_cast<std::string>(title)); };
		EventSystem::Get()->RegisterEventAction(EventType::SetWindowTitle, lambda);
		
		m_ctx = ctx;
		
		return m_handle;
	}
	
	void Window::SetTitle(const std::string& title)
	{
		SDL_SetWindowTitle(m_handle, title.c_str());
	}
	
	void Window::DestroyAll()
	{
		SDL_DestroyWindow(m_handle);
		SDL_Quit();
	}
	
	bool Window::ProcessEvents(double delta)
	{
		if (Console::close_app) return false;
		static int x, y, w, h, px, py = 0;
		static float dx, dy = 0.f;
		
		EventSystem* eventSystem = EventSystem::Get();
		Renderer* renderer = m_ctx->GetSystem<Renderer>();
		CameraSystem* cameraSystem = m_ctx->GetSystem<CameraSystem>();
		Camera* camera_main = cameraSystem->GetCamera(0);
		
		auto vulkan = m_ctx->GetVKContext();
		bool combineDirections = false;
		
		ImGuiIO& io = ImGui::GetIO();
		
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
			{
				//FIRE_EVENT(Event::OnExit);
				return false;
			}
			
			if (event.type == SDL_MOUSEWHEEL)
			{
				if (event.wheel.x > 0) io.MouseWheelH += 1;
				if (event.wheel.x < 0) io.MouseWheelH -= 1;
				if (event.wheel.y > 0) io.MouseWheel += 1;
				if (event.wheel.y < 0) io.MouseWheel -= 1;
			}
			
			if (event.type == SDL_MOUSEBUTTONDOWN)
			{
				if (event.button.button == SDL_BUTTON_LEFT) GUI::g_MousePressed[0] = true;
				if (event.button.button == SDL_BUTTON_RIGHT) GUI::g_MousePressed[1] = true;
				if (event.button.button == SDL_BUTTON_MIDDLE) GUI::g_MousePressed[2] = true;
			}
			
			if (event.type == SDL_TEXTINPUT)
				io.AddInputCharactersUTF8(event.text.text);
			
			if (event.type == SDL_KEYDOWN)
			{
				const int key = event.key.keysym.scancode;
				io.KeysDown[key] = true;
			}
			else if (event.type == SDL_KEYUP)
			{
				const int key = event.key.keysym.scancode;
				io.KeysDown[key] = false;
				io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
				io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
				io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
				io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
			}
			
			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				eventSystem->PushEvent(EventType::ScaleRenderTargets);
			}
		}
		
		
		if (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_RIGHT) && IsInsideRenderWindow(px, py))
		{
			dx = static_cast<float>(x - px);
			dy = static_cast<float>(y - py);
			camera_main->Rotate(dx, dy);
			WrapInsideRenderWindow(x, y);
		}
		
		px = x;
		py = y;
		
		if (io.KeysDown[SDL_SCANCODE_ESCAPE])
		{
			const SDL_MessageBoxButtonData buttons[] = {
					{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "cancel"},
					{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "yes"}
			};
			const SDL_MessageBoxColorScheme colorScheme = {
					{{255, 0, 0}, {0, 255, 0}, {255, 255, 0}, {0, 0, 255}, {255, 0, 255}}
			};
			const SDL_MessageBoxData messageboxdata = {
					SDL_MESSAGEBOX_INFORMATION, nullptr, "Exit", "Are you sure you want to exit?",
					SDL_arraysize(buttons), buttons, &colorScheme
			};
			int buttonid;
			SDL_ShowMessageBox(&messageboxdata, &buttonid);
			if (buttonid == 1)
				return false;
		}
		
		if ((io.KeysDown[SDL_SCANCODE_W] || io.KeysDown[SDL_SCANCODE_S]) &&
		    (io.KeysDown[SDL_SCANCODE_A] || io.KeysDown[SDL_SCANCODE_D]))
		{
			combineDirections = true;
		}
		const float velocity = combineDirections ? GUI::cameraSpeed * static_cast<float>(delta) * 0.707f :
		                       GUI::cameraSpeed * static_cast<float>(delta);
		if (io.KeysDown[SDL_SCANCODE_W]) camera_main->Move(Camera::RelativeDirection::FORWARD, velocity);
		if (io.KeysDown[SDL_SCANCODE_S]) camera_main->Move(Camera::RelativeDirection::BACKWARD, velocity);
		if (io.KeysDown[SDL_SCANCODE_A]) camera_main->Move(Camera::RelativeDirection::LEFT, velocity);
		if (io.KeysDown[SDL_SCANCODE_D]) camera_main->Move(Camera::RelativeDirection::RIGHT, velocity);
		
		if (eventSystem->PollEvent(EventType::CompileShaders))
		{
			renderer->RecreatePipelines();
		}
		
		if (eventSystem->PollEvent(EventType::ScaleRenderTargets))
		{
			if (!isMinimized())
			{
				SDL_GL_GetDrawableSize(vulkan->window, &w, &h);
				renderer->ResizeViewport(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
			}
		}
		
		eventSystem->ClearPushedEvents();
		
		return true;
	}
	
	void Window::WrapInsideRenderWindow(int& x, int& y)
	{
		Rect2D rect2D
				{
						static_cast<int>(GUI::winPos.x),
						static_cast<int>(GUI::winPos.y),
						static_cast<int>(GUI::winSize.x),
						static_cast<int>(GUI::winSize.y)
				};
		
		if (x < rect2D.x)
		{
			x = rect2D.x + rect2D.width;
		}
		else if (x > rect2D.x + rect2D.width)
		{
			x = rect2D.x;
		}
		
		if (y < rect2D.y)
		{
			y = rect2D.y + rect2D.height;
		}
		else if (y > rect2D.y + rect2D.height)
		{
			y = rect2D.y;
		}
		
		SDL_WarpMouseInWindow(m_handle, x, y);
	}
	
	bool Window::IsInsideRenderWindow(int x, int y)
	{
		Rect2D rect2D
				{
						static_cast<int>(GUI::winPos.x),
						static_cast<int>(GUI::winPos.y),
						static_cast<int>(GUI::winSize.x),
						static_cast<int>(GUI::winSize.y)
				};
		
		return x >= rect2D.x && y >= rect2D.y && x <= rect2D.x + rect2D.width &&
		       y <= rect2D.y + rect2D.height;
	}
	
	bool Window::isMinimized()
	{
		return (SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MINIMIZED) != 0;
	}
}
