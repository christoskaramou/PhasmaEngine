#pragma once

#include "../Core/Image.h"
#include "../Pipeline/Pipeline.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <functional>
#include <map>

namespace vk
{
	class DescriptorSet;
	class DescriptorSetLayout;
	class CommandBuffer;
}

namespace vm
{
	class Bloom
	{
	public:
		Bloom();
		~Bloom();

		std::vector<Framebuffer> framebuffers{};
		Pipeline pipelineBrightFilter;
		Pipeline pipelineGaussianBlurHorizontal;
		Pipeline pipelineGaussianBlurVertical;
		Pipeline pipelineCombine;
		RenderPass renderPassBrightFilter;
		RenderPass renderPassGaussianBlur;
		RenderPass renderPassCombine;
		Ref_t<vk::DescriptorSet> DSBrightFilter;
		Ref_t<vk::DescriptorSet> DSGaussianBlurHorizontal;
		Ref_t<vk::DescriptorSet> DSGaussianBlurVertical;
		Ref_t<vk::DescriptorSet> DSCombine;
		Ref_t<vk::DescriptorSetLayout> DSLayoutBrightFilter;
		Ref_t<vk::DescriptorSetLayout> DSLayoutGaussianBlurHorizontal;
		Ref_t<vk::DescriptorSetLayout> DSLayoutGaussianBlurVertical;
		Ref_t<vk::DescriptorSetLayout> DSLayoutCombine;
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
