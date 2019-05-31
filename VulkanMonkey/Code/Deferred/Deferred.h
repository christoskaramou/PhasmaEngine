#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Light/Light.h"
#include "../GUI/GUI.h"
#include "../Shadows/Shadows.h"
#include "../SkyBox/Skybox.h"
#include "../Vertex/Vertex.h"
#include "../Model/Model.h"
#include "../Mesh/Mesh.h"
#include "../Swapchain/Swapchain.h"
#include <map>

namespace vm {
	struct Deferred
	{
		VulkanContext* vulkan = &VulkanContext::get();

		vk::RenderPass renderPass, compositionRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, compositionFrameBuffers{};
		vk::DescriptorSet DSComposition;
		vk::DescriptorSetLayout DSLayoutComposition;
		Pipeline pipeline;
		Pipeline pipelineComposition;

		void batchStart(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		void batchEnd();
		void createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, Shadows& shadows, SkyBox& skybox, mat4& invViewProj, const vk::Extent2D& extent);
		void createRenderPasses(std::map<std::string, Image>& renderTargets);
		void createGBufferRenderPasses(std::map<std::string, Image>& renderTargets);
		void createCompositionRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createGBufferFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createCompositionFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipelines(std::map<std::string, Image>& renderTargets);
		void createGBufferPipeline(std::map<std::string, Image>& renderTargets);
		void createCompositionPipeline(std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}