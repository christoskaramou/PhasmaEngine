#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../GUI/GUI.h"
#include <vector>
#include <map>
#include <string>

namespace vm {
	struct FXAA
	{
		VulkanContext* vulkan = &VulkanContext::get();

		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		vk::RenderPass renderPass;
		vk::DescriptorSet DSet;
		vk::DescriptorSetLayout DSLayout;
		Image frameImage;

		void Init();
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage);
		void destroy();
	};
}