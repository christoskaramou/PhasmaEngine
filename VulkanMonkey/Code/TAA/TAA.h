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
	struct TAA
	{
		VulkanContext* vulkan = &VulkanContext::get();

		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		vk::RenderPass renderPass;
		vk::DescriptorSet DSet;
		vk::DescriptorSetLayout DSLayout;
		Image previous;
		bool initialized = false;

		void Init();
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(uint32_t imageIndex);
		void createRenderPasses(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipelines(std::map<std::string, Image>& renderTargets);
		void copyImage(const vk::CommandBuffer& cmd, Image& source);
		void destroy();
	};
}