#pragma once
#include "../Model/Object.h"

namespace vk
{
	class DescriptorSet;
}

namespace vm
{
	class SkyBox
	{
	public:
		SkyBox();
		~SkyBox();
		Image texture;
		Ref<vk::DescriptorSet> descriptorSet;

		void createDescriptorSet();
		void loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show = true);
		void loadTextures(const std::array<std::string, 6>& paths, int imageSideSize);
		void destroy();
	};
}