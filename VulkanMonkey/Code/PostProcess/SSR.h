#pragma once

#include "../Core/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Core/Image.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <vector>
#include <map>

namespace vk
{
	class DescriptorSet;
	class DescriptorSetLayout;
	class CommandBuffer;
	struct Extent2D;
}

namespace vm
{
	class SSR
	{
	public:
		SSR();
		~SSR();
		mat4 reflectionInput[4];
		Buffer UBReflection;
		std::vector<Framebuffer> framebuffers{};
		Pipeline pipeline;
		RenderPass renderPass;
		Ref_t<vk::DescriptorSet> DSet;
		Ref_t<vk::DescriptorSetLayout> DSLayout;

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