#pragma once

#include "../Pipeline/Pipeline.h"
#include <map>
#include <functional>
#include "../Core/Image.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include "../VulkanContext/VulkanContext.h"
#include <vulkan/vulkan.hpp>

namespace vm
{
	class Bloom
	{
	public:
		Bloom() = default;
		~Bloom() = default;

		std::vector<Framebuffer> framebuffers{};
		Pipeline pipelineBrightFilter;
		Pipeline pipelineGaussianBlurHorizontal;
		Pipeline pipelineGaussianBlurVertical;
		Pipeline pipelineCombine;
		RenderPass renderPassBrightFilter;
		RenderPass renderPassGaussianBlur;
		RenderPass renderPassCombine;
		vk::DescriptorSet DSBrightFilter;
		vk::DescriptorSet DSGaussianBlurHorizontal;
		vk::DescriptorSet DSGaussianBlurVertical;
		vk::DescriptorSet DSCombine;
		vk::DescriptorSetLayout DSLayoutBrightFilter;
		vk::DescriptorSetLayout DSLayoutGaussianBlurHorizontal;
		vk::DescriptorSetLayout DSLayoutGaussianBlurVertical;
		vk::DescriptorSetLayout DSLayoutCombine;
		Image frameImage;

		void Init();
		void copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		void createRenderPasses(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipelines(std::map<std::string, Image>& renderTargets);
		void createBrightFilterPipeline(std::map<std::string, Image>& renderTargets);
		void createGaussianBlurHorizontaPipeline(std::map<std::string, Image>& renderTargets);
		void createGaussianBlurVerticalPipeline(std::map<std::string, Image>& renderTargets);
		void createCombinePipeline(std::map<std::string, Image>& renderTargets);
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, uint32_t totalImages, std::function<void(vk::CommandBuffer, Image&, LayoutState)>&& changeLayout, std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}
