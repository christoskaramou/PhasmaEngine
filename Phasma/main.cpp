/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "Window/Window.h"
#include "Core/Timer.h"
#include "ECS/Context.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Systems/EventSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Renderer/RHI.h"

using namespace pe;

int main(int argc, char* argv[])
{
	//freopen("log.txt", "w", stdout);
	//freopen("errors.txt", "w", stderr);

	Context& context = *CONTEXT;
	context.CreateSystem<EventSystem>();

	Window* window = Window::Create();
	VULKAN.Init(window->Handle()); // TODO: Remove this from here (was in Renderer)

	auto* cs = context.CreateSystem<CameraSystem>();
	auto* ls = context.CreateSystem<LightSystem>();
	auto* rs = context.CreateSystem<RendererSystem>();
	auto* pps = context.CreateSystem<PostProcessSystem>();

	context.InitSystems();

	FrameTimer& frameTimer = FrameTimer::Instance();

	while (true)
	{
		frameTimer.Start();
		
		if (!window->ProcessEvents(frameTimer.delta))
			break;
		
		if (!window->isMinimized())
		{
			context.UpdateSystems(frameTimer.delta);

			Queue<Launch::AsyncNoWait>::ExecuteRequests();
			Queue<Launch::Async>::ExecuteRequests();
			Queue<Launch::SyncDeferred>::ExecuteRequests();
			Queue<Launch::AsyncDeferred>::ExecuteRequests();
			frameTimer.timestamps[0] = static_cast<float>(frameTimer.Count());

			context.DrawSystems();
		}
		
		frameTimer.Delay(1.0 / static_cast<double>(GUI::fps) - frameTimer.Count());
		frameTimer.Tick();
	}

	context.DestroySystems();
	delete window;

	return 0;
}