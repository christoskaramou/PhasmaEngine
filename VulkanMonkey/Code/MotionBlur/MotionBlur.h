#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Surface/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../GUI/GUI.h"
#include "../Timer/Timer.h"
#include "../Camera/Camera.h"
#include <vector>
#include <string>
#include <map>

namespace vm {
	struct MotionBlur
	{
		VulkanContext* vulkan = &VulkanContext::get();

		Buffer UBmotionBlur;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		vk::RenderPass renderPass;
		vk::DescriptorSet DSMotionBlur;
		vk::DescriptorSetLayout DSLayoutMotionBlur;
		Image frameImage;
		
		void Init();
		void update(Camera& camera) const;
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		void createMotionBlurUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		void destroy();
	};
}