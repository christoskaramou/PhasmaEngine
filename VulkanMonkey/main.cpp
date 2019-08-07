#include "Code/Window/Window.h"
#include "Code/Event/Event.h"
#include "Code/Timer/Timer.h"

using namespace vm;

int main(int argc, char* argv[])
{
	//freopen("log.txt", "w", stdout);
	//freopen("errors.txt", "w", stderr);

	Window::create();

	while (true)
	{
		Timer timer;
		FIRE_EVENT(Event::FrameStart);
		timer.minFrameTime(1.f / static_cast<float>(GUI::fps));

		if (!Window::processEvents(timer.getDelta(GUI::timeScale)))
			break;

		for (auto& renderer : Window::renderer) {
			renderer->update(timer.getDelta(GUI::timeScale));
			renderer->present();
		}
		if (timer.intervalsOf(0.75f)) {
			FIRE_EVENT(Event::Tick);
			GUI::cpuWaitingTime = Timer::waitingTime * 1000.f; // secs to ms
			std::chrono::duration<float> dur = std::chrono::high_resolution_clock::now() - Timer::frameStart;
			GUI::cpuTime = dur.count() * 1000.f - GUI::cpuWaitingTime;
			for (int i = 0; i < GUI::metrics.size(); i++)
				GUI::stats[i] = GUI::metrics[i];
		}
		FIRE_EVENT(Event::FrameEnd, timer.getDelta());
	}

	return 0;
}