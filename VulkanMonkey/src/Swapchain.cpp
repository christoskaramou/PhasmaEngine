#include "../include/Swapchain.h"
#include <iostream>

using namespace vm;

void Swapchain::destroy(vk::Device device)
{
	for (auto &image : images) {
		device.destroyImageView(image.view);
		image.view = nullptr;
	}
	if (swapchain) {
		device.destroySwapchainKHR(swapchain);
		swapchain = nullptr;
		std::cout << "Swapchain destroyed\n";
	}
}