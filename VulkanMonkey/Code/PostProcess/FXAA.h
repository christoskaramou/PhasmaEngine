#pragma once

#include "../Pipeline/Pipeline.h"
#include "../Core/Image.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include "../VulkanContext/VulkanContext.h"
#include <vector>
#include <map>
#include <string>

namespace vm
{
	class FXAA
	{
	public:
		std::vector<Framebuffer> framebuffers{};
		Pipeline pipeline;
		RenderPass renderPass;
		vk::DescriptorSet DSet;
		vk::DescriptorSetLayout DSLayout;
		Image frameImage;

		void Init();
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets) const;
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		void destroy();
	};
}