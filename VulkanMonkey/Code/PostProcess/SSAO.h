#pragma once

#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <map>
#include <functional>

namespace vm {
	struct SSAO
	{
		mat4 pvm[3];
		Buffer UB_Kernel;
		Buffer UB_PVM;
		Image noiseTex;
		RenderPass renderPass, blurRenderPass;
		std::vector<Framebuffer> framebuffers{}, blurFramebuffers{};
		Pipeline pipeline;
		Pipeline pipelineBlur;
		vk::DescriptorSetLayout DSLayout, DSLayoutBlur;
		vk::DescriptorSet DSet, DSBlur;

		void update(Camera& camera);
		void createRenderPasses(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createSSAOFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createSSAOBlurFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipelines(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void createBlurPipeline(std::map<std::string, Image>& renderTargets);
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, Image& image);
		void destroy();
	};
}