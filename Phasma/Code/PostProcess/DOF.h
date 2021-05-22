#pragma once

#include "../Renderer/Pipeline.h"
#include "../Core/Image.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <map>
#include <string>

namespace vk
{
	class DescriptorSet;
	
	class CommandBuffer;
}

namespace pe
{
	class DOF
	{
	public:
		DOF();
		
		~DOF();
		
		std::vector<Framebuffer> framebuffers {};
		Pipeline pipeline;
		RenderPass renderPass;
		Ref<vk::DescriptorSet> DSet;
		Image frameImage;
		
		void Init();
		
		void createRenderPass(std::map<std::string, Image>& renderTargets);
		
		void createFrameBuffers(std::map<std::string, Image>& renderTargets);
		
		void createPipeline(std::map<std::string, Image>& renderTargets);
		
		void createUniforms(std::map<std::string, Image>& renderTargets);
		
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets);
		
		void destroy();
	};
}
