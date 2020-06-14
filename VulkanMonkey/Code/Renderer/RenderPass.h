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
		RenderPass() = default;
		~RenderPass() = default;

		void Create(const vk::Format& format, const vk::Format& depthFormat);
		void Create(const std::vector<vk::Format>& formats, const vk::Format& depthFormat);
		void Destroy();
		Ref<vk::RenderPass> GetRef() const;

	private:
		Ref<vk::RenderPass> m_renderPass;
	};
}