#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Surface/Surface.h"
#include "../Math/Math.h"
#include "../GUI/GUI.h"
#include "../Camera/Camera.h"
#include <vector>
#include <map>

namespace vm {
	struct SSR
	{
		VulkanContext* vulkan = &VulkanContext::get();

		Buffer UBReflection;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		vk::RenderPass renderPass;
		vk::DescriptorSet DSReflection;
		vk::DescriptorSetLayout DSLayoutReflection;

		void update(Camera& camera);
		void createSSRUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(uint32_t imageIndex);
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}