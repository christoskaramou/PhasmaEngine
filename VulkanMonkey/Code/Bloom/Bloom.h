#pragma once

#include "../Pipeline/Pipeline.h"
#include <map>
#include <functional>
#include "../Image/Image.h"

namespace vm {
	struct Bloom
	{
		Bloom() = default;
		~Bloom() = default;

		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipelineBrightFilter;
		Pipeline pipelineGaussianBlurHorizontal;
		Pipeline pipelineGaussianBlurVertical;
		Pipeline pipelineCombine;
		vk::RenderPass renderPassBrightFilter;
		vk::RenderPass renderPassGaussianBlur;
		vk::RenderPass renderPassCombine;
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
		void createBrightFilterRenderPass(std::map<std::string, Image>& renderTargets);
		void createGaussianBlurRenderPass(std::map<std::string, Image>& renderTargets);
		void createCombineRenderPass(std::map<std::string, Image>& renderTargets);
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
