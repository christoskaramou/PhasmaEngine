/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "CommandPool.h"
#include "Renderer/Vulkan/Vulkan.h"

namespace pe
{
	CommandPool::CommandPool() : m_handle {}
	{
	}

	CommandPool::CommandPool(CommandPoolHandle handle) : m_handle(handle)
	{
	}

	CommandPool::~CommandPool()
	{
	}

	void CommandPool::Create(uint32_t graphicsFamilyId)
	{
		VkCommandPoolCreateInfo cpci;
		cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpci.queueFamilyIndex = graphicsFamilyId;
		cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		VkCommandPool commandPool;
		vkCreateCommandPool(*VULKAN.device, &cpci, nullptr, &commandPool);
		m_handle = commandPool;
	}

	void CommandPool::Destroy()
	{
		if (m_handle)
		{
			vkDestroyCommandPool(*VULKAN.device, m_handle, nullptr);
			m_handle = {};
		}
	}

	CommandPoolHandle& CommandPool::Handle()
	{
		return m_handle;
	}
}