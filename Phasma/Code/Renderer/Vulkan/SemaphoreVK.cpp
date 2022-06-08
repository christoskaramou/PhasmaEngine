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
#include "Renderer/Semaphore.h"
#include "Renderer/RHI.h"

namespace pe
{
    Semaphore::Semaphore(bool timeline, const std::string &name)
    {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkSemaphoreTypeCreateInfoKHR ti{};
        m_timeline = timeline;
        if (timeline)
        {
            ti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
            ti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
            ti.initialValue = 0;
            si.pNext = &ti;
        }

        VkSemaphore semaphore;
        PE_CHECK(vkCreateSemaphore(RHII.GetDevice(), &si, nullptr, &semaphore));
        m_handle = semaphore;

        Debug::SetObjectName(m_handle, VK_OBJECT_TYPE_SEMAPHORE, name);
    }

    Semaphore::~Semaphore()
    {
        if (m_handle)
        {
            vkDestroySemaphore(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }
    }

    void Semaphore::Wait(uint64_t value)
    {
        if (!m_timeline)
            PE_ERROR("Semaphore::Wait() called on non-timeline semaphore!");

        VkSemaphore semaphore = m_handle;

        VkSemaphoreWaitInfo swi{};
        swi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
        swi.semaphoreCount = 1;
        swi.pSemaphores = &semaphore;
        swi.pValues = &value;

        PE_CHECK(vkWaitSemaphores(RHII.GetDevice(), &swi, UINT64_MAX));
    }

    void Semaphore::Signal(uint64_t value)
    {
        if (!m_timeline)
            PE_ERROR("Semaphore::Wait() called on non-timeline semaphore!");

        VkSemaphoreSignalInfo ssi;
        ssi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        ssi.pNext = NULL;
        ssi.semaphore = m_handle;
        ssi.value = value;

        PE_CHECK(vkSignalSemaphore(RHII.GetDevice(), &ssi));
    }
}
#endif
