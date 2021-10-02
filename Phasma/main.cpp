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
#include "Renderer/Vulkan/Vulkan.h"

using namespace pe;

int main(int argc, char* argv[])
{
	//freopen("log.txt", "w", stdout);
	//freopen("errors.txt", "w", stderr);

	Context& context = *Context::Get();
	context.CreateSystem<EventSystem>();

	Window window;
	VulkanContext::Get()->Init(window.Create()); // TODO: Remove this from here (was in Renderer)

	context.CreateSystem<LightSystem>();
	context.CreateSystem<RendererSystem>();
	context.CreateSystem<PostProcessSystem>();
	context.CreateSystem<CameraSystem>();

	context.InitSystems();

	FrameTimer& frame_timer = FrameTimer::Instance();

	while (true)
	{
		frame_timer.Start();
		
		if (!window.ProcessEvents(frame_timer.delta))
			break;
		
		if (!window.isMinimized())
		{
			context.UpdateSystems(frame_timer.delta);

			Queue<Launch::AsyncNoWait>::ExecuteRequests();
			Queue<Launch::Async>::ExecuteRequests();
			Queue<Launch::SyncDeferred>::ExecuteRequests();
			Queue<Launch::AsyncDeferred>::ExecuteRequests();
			frame_timer.timestamps[0] = static_cast<float>(frame_timer.Count());

			context.DrawSystems();
		}
		
		frame_timer.Delay(1.0 / static_cast<double>(GUI::fps) - frame_timer.Count());
		frame_timer.Tick();
	}

	context.DestroySystems();

	return 0;
}