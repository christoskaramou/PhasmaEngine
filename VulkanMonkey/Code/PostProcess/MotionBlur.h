#pragma once

#include "../Core/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Core/Image.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include "../VulkanContext/VulkanContext.h"
#include <vector>
#include <string>
#include <map>

namespace vm
{
	class MotionBlur
	{
	public:
		mat4 motionBlurInput[4];
		Buffer UBmotionBlur;
		std::vector<Framebuffer> framebuffers{};
		Pipeline pipeline;
		RenderPass renderPass;
		vk::DescriptorSet DSMotionBlur;
		vk::DescriptorSetLayout DSLayoutMotionBlur;
		Image frameImage;
		
		void Init();
		void update(Camera& camera);
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