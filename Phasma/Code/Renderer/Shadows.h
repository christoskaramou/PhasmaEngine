#pragma once

#include "Buffer.h"
#include "Image.h"
#include "Pipeline.h"
#include "../Core/Math.h"
#include "../Camera/Camera.h"
#include "RenderPass.h"
#include "Framebuffer.h"

namespace vk
{
	class DescriptorSet;
}

namespace pe
{
	struct ShadowsUBO
	{
		mat4 projection, view;
		float castShadows;
		float maxCascadeDist0;
		float maxCascadeDist1;
		float maxCascadeDist2;
	};
	
	class Shadows
	{
	public:
		Shadows();
		
		~Shadows();
		
		ShadowsUBO shadows_UBO[3] {};
		static uint32_t imageSize;
		RenderPass renderPass;
		std::vector<Image> textures {};
		Ref<std::vector<vk::DescriptorSet>> descriptorSets;
		std::vector<Framebuffer> framebuffers {};
		std::vector<Buffer> uniformBuffers {};
		Pipeline pipeline;
		
		void update(Camera& camera);
		
		void createUniformBuffers();
		
		void createDescriptorSets();
		
		void createRenderPass();
		
		void createFrameBuffers();
		
		void createPipeline();
		
		void destroy();
	};
}
