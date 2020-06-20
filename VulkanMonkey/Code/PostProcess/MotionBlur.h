#pragma once

#include "../Core/Buffer.h"
#include "../Renderer/Pipeline.h"
#include "../Core/Image.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <vector>
#include <string>
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
	class MotionBlur
	{
	public:
		MotionBlur();
		~MotionBlur();
		mat4 motionBlurInput[4];
		Buffer UBmotionBlur;
		std::vector<Framebuffer> framebuffers{};
		Pipeline pipeline;
		RenderPass renderPass;
		Ref_t<vk::DescriptorSet> DSet;
		Ref_t<vk::DescriptorSetLayout> DSLayout;
		Image frameImage;
		
		void Init();
		void update(Camera& camera);
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		void createPipeline(std::map<std::string, Image>& renderTargets);
		void copyFrameImage(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		void createMotionBlurUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		void destroy();
	};
}