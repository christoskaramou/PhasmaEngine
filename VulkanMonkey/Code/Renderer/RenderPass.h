#pragma once
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	class RenderPass
	{
	public:
		RenderPass() = default;
		~RenderPass() = default;

		void Create(vk::Format format, vk::Format depthFormat = vk::Format::eUndefined);
		void Create(const std::vector<vk::Format>& formats, vk::Format depthFormat = vk::Format::eUndefined);
		void Destroy();
		Ref<vk::RenderPass> GetRef() const;

	private:
		Ref<vk::RenderPass> m_renderPass;
	};
}