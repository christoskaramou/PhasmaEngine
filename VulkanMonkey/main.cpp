#include "Code/Window/Window.h"
#include "Code/Event/Event.h"
#include "Code/Timer/Timer.h"

using namespace vm;

int main(int argc, char* argv[])
{
	//freopen("log.txt", "w", stdout);
	//freopen("errors.txt", "w", stderr);

	Window::create();

	FrameTimer& frame_timer = FrameTimer::Instance();

	while (true)
	{
		frame_timer.Start();
		FIRE_EVENT(Event::FrameStart);

		if (!Window::processEvents(frame_timer.delta))
			break;

		for (auto& renderer : Window::renderer) {
			renderer->update(frame_timer.delta);
			renderer->present();
		}

		static double interval = 0.0;
		interval += frame_timer.delta;
		if (interval > 0.75) {
			interval = 0.0;
			GUI::cpuWaitingTime = SECONDS_TO_MILLISECONDS(frame_timer.measures[0]);
			GUI::updatesTime = SECONDS_TO_MILLISECONDS(GUI::updatesTimeCount);
			GUI::cpuTime = static_cast<float>(frame_timer.delta * 1000.0) - GUI::cpuWaitingTime;
			for (int i = 0; i < GUI::metrics.size(); i++)
				GUI::stats[i] = GUI::metrics[i];
		}
		FIRE_EVENT(Event::FrameEnd, frame_timer.delta);
		frame_timer.Delay(1.0 / static_cast<double>(GUI::fps) - frame_timer.Count());
		frame_timer.Tick();
	}

	return 0;
}