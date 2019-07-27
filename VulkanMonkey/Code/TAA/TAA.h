#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../GUI/GUI.h"
#include "../Math/Math.h"
#include "../Camera/Camera.h"
#include <vector>
#include <map>
#include <string>
#include <functional>

namespace vm {
	struct TAA
	{
		VulkanContext* vulkan = &VulkanContext::get();

		std::vector<vk::Framebuffer> frameBuffers{}, frameBuffersSharpen{};
		Pipeline pipeline, pipelineSharpen;
		vk::RenderPass renderPass, renderPassSharpen;
		vk::DescriptorSet DSet, DSetSharpen;
		vk::DescriptorSetLayout DSLayout, DSLayoutSharpen;
		Image previous;
		Image frameImage;

		struct UBO { vec4 values; vec4 sharpenValues; mat4 invProj; }ubo;
		Buffer uniform;

		void Init();
		void update(const Camera& camera);
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, std::map<std::string, Image>& renderTargets);
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createRenderPassSharpen(std::map<std::string, Image>& renderTargets);
		void createRenderPasses(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void createPipelineSharpen(std::map<std::string, Image>& renderTargets);
		void createPipelines(std::map<std::string, Image>& renderTargets);
		void copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		void saveImage(const vk::CommandBuffer& cmd, Image& source) const;
		void destroy();
	};
}