#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Object/Object.h"
#include "../Pipeline/Pipeline.h"
#include "../../include/tinygltf/stb_image.h"
#include "../Math/Math.h"
#include "../Camera/Camera.h"

namespace vm {
	struct SkyBox : Object
	{
		Pipeline pipeline;
		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};

		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		void loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show = true);
		void update(Camera& camera);
		void draw(uint32_t imageIndex);
		void loadTextures(const std::array<std::string, 6>& paths, uint32_t imageSideSize);
		void destroy();
	};
}