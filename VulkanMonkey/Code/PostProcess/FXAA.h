#pragma once

#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Renderer/RenderPass.h"
#include <vector>
#include <map>
#include <string>

namespace vm {
	struct FXAA
	{
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		Ref<RenderPass> renderPass;
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