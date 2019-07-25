#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Object/Object.h"
#include "../Pipeline/Pipeline.h"
#include "../../include/tinygltf/stb_image.h"
#include "../Math/Math.h"
#include "../Camera/Camera.h"

//#define RENDER_SKYBOX

namespace vm {
	struct SkyBox : Object
	{
		Pipeline pipeline;
		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout();

#ifdef RENDER_SKYBOX
		void update(Camera& camera);
		void draw(uint32_t imageIndex);
		void createRenderPass();
		void createFrameBuffers();
		void createPipeline();
#endif
		void loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show = true);
		void loadTextures(const std::array<std::string, 6>& paths, int imageSideSize);
		void destroy();
	};
}