#pragma once
#include "../Core/Base.h"
#include <vector>

namespace vk
{
	enum class Format;
	class RenderPass;
}

namespace vm
{
	class RenderPass
	{
	public:
		RenderPass();
		~RenderPass();

		void Create(const vk::Format& format, const vk::Format& depthFormat);
		void Create(const std::vector<vk::Format>& formats, const vk::Format& depthFormat);
		void Destroy();

		Ref<vk::RenderPass> renderPass;
	};
}