#pragma once

#include "../Renderer/Pipeline.h"
#include "../Core/Image.h"
#include "../Core/Light.h"
#include "../Shadows/Shadows.h"
#include "../Skybox/Skybox.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <map>

namespace vk
{
	class DescriptorSet;
	class DescriptorSetLayout;
}

namespace vm
{
	class Deferred
	{
	public:
		Deferred();
		~Deferred();
		RenderPass renderPass, compositionRenderPass;
		std::vector<Framebuffer> framebuffers{}, compositionFramebuffers{};
		Ref<vk::DescriptorSet> DSComposition;
		Pipeline pipeline;
		Pipeline pipelineComposition;
		Image ibl_brdf_lut;

		struct UBO { vec4 screenSpace[8]; } ubo;
		Buffer uniform;

		void batchStart(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		static void batchEnd();
		void createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void update(mat4& invViewProj);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, Shadows& shadows, SkyBox& skybox, const vk::Extent2D& extent);
		void createRenderPasses(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createGBufferFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createCompositionFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipelines(std::map<std::string, Image>& renderTargets);
		void createGBufferPipeline(std::map<std::string, Image>& renderTargets);
		void createCompositionPipeline(std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}