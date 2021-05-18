#pragma once

#include "../Renderer/Pipeline.h"
#include "../Core/Image.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <vector>
#include <map>
#include <string>

namespace vk
{
	class DescriptorSet;
	class CommandBuffer;
	struct Extent2D;
}

namespace pe
{
	class FXAA
	{
	public:
		FXAA();

		~FXAA();

		std::vector<Framebuffer> framebuffers {};
		Pipeline pipeline;
		RenderPass renderPass;
		Ref<vk::DescriptorSet> DSet;
		Image frameImage;

		void Init();

		void createUniforms(std::map<std::string, Image>& renderTargets);

		void updateDescriptorSets(std::map<std::string, Image>& renderTargets) const;

		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);

		void createRenderPass(std::map<std::string, Image>& renderTargets);

		void createFrameBuffers(std::map<std::string, Image>& renderTargets);

		void createPipeline(std::map<std::string, Image>& renderTargets);

		void destroy();
	};
}