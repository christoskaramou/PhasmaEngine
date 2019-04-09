#pragma once

#include "../Image/Image.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Camera/Camera.h"
#include "../VulkanContext/VulkanContext.h"
#include <map>

namespace vm {
	struct SBOOut
	{
		uint32_t pLeft = 0;
		uint32_t pRight = 0;
		uint32_t dummy[2];
	};
	struct SBOIn : SBOOut
	{
		vec4 boundingShpere;
	};
	struct UBOIn
	{
		mat4 invViewProj;
		vec4 data; // x,y,z for camPos and w for primitive count
		vec4 windowSize;
	};
	struct Compute
	{
		VulkanContext* vulkan = &VulkanContext::get();

		std::vector<SBOIn> sboIn{};
		UBOIn uboIn;

		Buffer SBIn;
		Buffer SBOut;
		Buffer UBIn;
		vk::DescriptorSet DSCompute;
		static vk::DescriptorSetLayout DSLayoutCompute;
		Pipeline pipeline;

		static vk::DescriptorSetLayout getDescriptorLayout();
		void update(std::map<std::string, Image>& renderTargets);
		void createComputeUniforms(size_t size, std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}