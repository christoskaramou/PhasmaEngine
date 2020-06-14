#pragma once

#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include <vector>
#include <map>

namespace vm {
	struct SSR
	{
		mat4 reflectionInput[4];
		Buffer UBReflection;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		Ref<RenderPass> renderPass;
		vk::DescriptorSet DSReflection;
		vk::DescriptorSetLayout DSLayoutReflection;

		void update(Camera& camera);
		void createSSRUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}