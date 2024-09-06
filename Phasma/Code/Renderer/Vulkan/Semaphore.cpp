#if PE_VULKAN
#include "Renderer/Semaphore.h"
#include "Renderer/RHI.h"

namespace pe
{
    Semaphore::Semaphore(bool timeline, const std::string &name)
        : m_timeline(timeline)
    {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkSemaphoreTypeCreateInfoKHR ti{};
        if (timeline)
        {
            ti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
            ti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
            ti.initialValue = 0;
            si.pNext = &ti;
        }

        VkSemaphore semaphore;
        PE_CHECK(vkCreateSemaphore(RHII.GetDevice(), &si, nullptr, &semaphore));
        m_apiHandle = semaphore;

        Debug::SetObjectName(m_apiHandle, name);
    }

    Semaphore::~Semaphore()
    {
        if (m_apiHandle)
        {
            vkDestroySemaphore(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }

    void Semaphore::Wait(uint64_t value)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::Wait() called on non-timeline semaphore!");

        VkSemaphore semaphore = m_apiHandle;
        VkSemaphoreWaitInfo swi{};
        swi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
        swi.semaphoreCount = 1;
        swi.pSemaphores = &semaphore;
        swi.pValues = &value;

        PE_CHECK(vkWaitSemaphores(RHII.GetDevice(), &swi, UINT64_MAX));
    }

    void Semaphore::Signal(uint64_t value)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::Signal() called on non-timeline semaphore!");

        VkSemaphoreSignalInfo ssi{};
        ssi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        ssi.semaphore = m_apiHandle;
        ssi.value = value;

        PE_CHECK(vkSignalSemaphore(RHII.GetDevice(), &ssi));
    }

    uint64_t Semaphore::GetValue()
    {
        if (!m_timeline)
            return 0;

        uint64_t value;
        vkGetSemaphoreCounterValue(RHII.GetDevice(), m_apiHandle, &value);
        return value;
    }
}
#endif
