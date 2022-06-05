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

#if PE_VULKAN
#include "Renderer/Fence.h"
#include "Renderer/RHI.h"

namespace pe
{
    Fence::Fence(bool signaled, const std::string &name)
    {
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

        VkFence fence;
        PE_CHECK(vkCreateFence(RHII.device, &fi, nullptr, &fence));
        m_handle = fence;

        m_canReturnToPool = false;

        Debug::SetObjectName(m_handle, VK_OBJECT_TYPE_FENCE, name);

        this->name = name;
    }

    Fence::~Fence()
    {
        if (m_handle)
        {
            vkDestroyFence(RHII.device, m_handle, nullptr);
            m_handle = {};
        }
    }

    void Fence::Wait()
    {
        if (m_submitted && GetStatus() == FenceStatus::Unsignaled)
        {
            VkFence fenceVK = m_handle;
            if (vkWaitForFences(RHII.device, 1, &fenceVK, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
                PE_ERROR("wait fences error!");
        }
    }

    void Fence::Reset()
    {
        VkFence fenceVK = m_handle;
        PE_CHECK(vkResetFences(RHII.device, 1, &fenceVK));
        m_canReturnToPool = true;
        m_submitted = false;
    }

    FenceStatus Fence::GetStatus()
    {
        auto res = vkGetFenceStatus(RHII.device, m_handle);
        switch (res)
        {
        case VK_SUCCESS:
            return FenceStatus::Signaled;
        case VK_NOT_READY:
            return FenceStatus::Unsignaled;
        default:
            PE_ERROR("get fence status error!");
            return FenceStatus::Error;
        }
    }

    void Fence::Init()
    {
    }

    void Fence::Clear()
    {
        for (auto &fence : s_allFences)
            Fence::Destroy(fence.second);

        s_allFences.clear();
        s_availableFences.clear();
        s_returnFences.clear();
    }

    void Fence::CheckReturns()
    {
        for (auto it = s_returnFences.begin(); it != s_returnFences.end();)
        {
            if (it->second->m_canReturnToPool)
            {
                s_availableFences.insert(*it);
                it = s_returnFences.erase(it);
            }
            else
                ++it;
        }
    }

    Fence *Fence::GetNext()
    {
        CheckReturns();

        if (s_availableFences.empty())
        {
            Fence *fence = Fence::Create(false, "Pool_fence_" + std::to_string(s_allFences.size()));
            s_availableFences[fence] = fence;
            s_allFences[fence] = fence;
        }

        Fence *fence = s_availableFences.begin()->second;
        fence->m_canReturnToPool = false;
        s_availableFences.erase(fence);
        return fence;
    }

    void Fence::Return(Fence *fence)
    {
        if (!fence || !fence->Handle())
            return;

        if (!fence->m_canReturnToPool)
            s_returnFences[fence] = fence;
        else
            s_availableFences[fence] = fence;
    }
}
#endif
