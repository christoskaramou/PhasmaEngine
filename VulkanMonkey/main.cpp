#include "Code/Window/Window.h"
#include "Code/Core/Timer.h"
#include <iostream>
#include "Code/ECS/Context.h"

using namespace vm;

int main(int argc, char* argv[])
{
	//freopen("log.txt", "w", stdout);
	//freopen("errors.txt", "w", stderr);

	Window window;
	Context ctx;
	ctx.CreateSystem<Renderer>(&ctx, window.Create(&ctx));
	ctx.GetSystem<Renderer>()->Init();

	Timer interval;
	interval.Start();

	FrameTimer& frame_timer = FrameTimer::Instance();

	while (true)
	{
		frame_timer.Start();
		
		if (!window.ProcessEvents(frame_timer.delta))
			break;
		
		if (!window.isMinimized())
		{
			ctx.GetSystem<Renderer>()->Update(frame_timer.delta);
			ctx.GetSystem<Renderer>()->Draw();
		}
		
		// Metrics every 0.75 sec
		if (interval.Count() > 0.75) {
			interval.Start();
			GUI::cpuWaitingTime = SECONDS_TO_MILLISECONDS<float>(frame_timer.timestamps[0]);
			GUI::updatesTime = SECONDS_TO_MILLISECONDS<float>(GUI::updatesTimeCount);
			GUI::cpuTime = static_cast<float>(frame_timer.delta * 1000.0) - GUI::cpuWaitingTime;
			for (int i = 0; i < GUI::metrics.size(); i++)
				GUI::stats[i] = GUI::metrics[i];
		}
		
		frame_timer.Delay(1.0 / static_cast<double>(GUI::fps) - frame_timer.Count());
		frame_timer.Tick();
	}

	return 0;
}