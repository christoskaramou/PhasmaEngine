#include "../include/Swapchain.h"
#include <iostream>

using namespace vm;

vm::Swapchain::Swapchain(VulkanContext * vulkan) :vulkan(vulkan)
{ }

void Swapchain::destroy()
{
	for (auto &image : images) {
		vulkan->device.destroyImageView(image.view);
		image.view = nullptr;
	}
	if (swapchain) {
		vulkan->device.destroySwapchainKHR(swapchain);
		swapchain = nullptr;
		std::cout << "Swapchain destroyed\n";
	}
}