#pragma once

#include "../Renderer/Buffer.h"
#include "../Renderer/Pipeline.h"
#include "../Renderer/Image.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <vector>
#include <string>
#include <map>

namespace vk
{
	class DescriptorSet;
	
	class CommandBuffer;
	
	struct Extent2D;
}

namespace pe
{
	class MotionBlur
	{
	public:
		MotionBlur();
		
		~MotionBlur();
		
		mat4 motionBlurInput[4];
		Buffer UBmotionBlur;
		std::vector<Framebuffer> framebuffers {};
		Pipeline pipeline;
		RenderPass renderPass;
		Ref<vk::DescriptorSet> DSet;
		Image frameImage;
		
		void Init();
		
		void update(Camera& camera);
		
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		
		void createPipeline(std::map<std::string, Image>& renderTargets);
		
		void createMotionBlurUniforms(std::map<std::string, Image>& renderTargets);
		
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent);
		
		void destroy();
	};
}