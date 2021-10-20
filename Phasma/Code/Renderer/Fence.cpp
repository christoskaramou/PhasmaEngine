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

#include "Fence.h"
#include "Renderer/Vulkan/Vulkan.h"

namespace pe
{
	Fence::Fence()
	{
		handle = {};
	}

	Fence::~Fence()
	{
	}

	void Fence::Create(bool signaled)
	{
		VkFenceCreateInfo fi{};
		fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fi.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

		VkFence fence;
		vkCreateFence(*VULKAN.device, &fi, nullptr, &fence);
		handle = fence;
	}

	void Fence::Destroy()
	{
		if (handle)
		{
			vkDestroyFence(*VULKAN.device, handle, nullptr);
			handle = {};
		}
	}
}