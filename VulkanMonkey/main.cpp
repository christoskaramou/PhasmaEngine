#include "Code/Window/Window.h"
#include "Code/Event/Event.h"
#include "Code/Timer/Timer.h"
#include <iostream>

using namespace vm;

int main(int argc, char* argv[])
{
	//freopen("log.txt", "w", stdout);
	//freopen("errors.txt", "w", stderr);

	Window::create();

	FrameTimer& frame_timer = FrameTimer::Instance();

	while (true)
	{
		try {
			frame_timer.Start();

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
				GUI::cpuWaitingTime = SECONDS_TO_MILLISECONDS(frame_timer.timestamps[0]);
				GUI::updatesTime = SECONDS_TO_MILLISECONDS(GUI::updatesTimeCount);
				GUI::cpuTime = static_cast<float>(frame_timer.delta * 1000.0) - GUI::cpuWaitingTime;
				for (int i = 0; i < GUI::metrics.size(); i++)
					GUI::stats[i] = GUI::metrics[i];
			}
			frame_timer.Delay(1.0 / static_cast<double>(GUI::fps) - frame_timer.Count());
			frame_timer.Tick();
		}
		catch (const vk::OutOfHostMemoryError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::OutOfDeviceMemoryError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::InitializationFailedError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::DeviceLostError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::MemoryMapFailedError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::LayerNotPresentError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::ExtensionNotPresentError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::FeatureNotPresentError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::IncompatibleDriverError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::TooManyObjectsError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::FormatNotSupportedError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::FragmentedPoolError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::OutOfPoolMemoryError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::InvalidExternalHandleError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::SurfaceLostKHRError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::NativeWindowInUseKHRError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::OutOfDateKHRError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::IncompatibleDisplayKHRError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::ValidationFailedEXTError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::InvalidShaderNVError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::InvalidDrmFormatModifierPlaneLayoutEXTError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::FragmentationEXTError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::NotPermittedEXTError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::FullScreenExclusiveModeLostEXTError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::InvalidOpaqueCaptureAddressKHRError & e) { std::cout << e.what() << '\n'; }
		catch (const vk::SystemError & e) { std::cout << e.what() << '\n'; }
		catch (const std::exception & e) { std::cout << e.what() << '\n'; }
	}

	return 0;
}