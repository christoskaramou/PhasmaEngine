#pragma once

#include "../Pipeline/Pipeline.h"
#include <map>
#include "../Image/Image.h"
#include "../Renderer/RenderPass.h"

namespace vm {
	struct DOF
	{
		DOF() = default;
		~DOF() = default;

		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		Ref<RenderPass> renderPass;
		vk::DescriptorSet DSet;
		vk::DescriptorSetLayout DSLayout;
		Image frameImage;

		void Init();
		void copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}
