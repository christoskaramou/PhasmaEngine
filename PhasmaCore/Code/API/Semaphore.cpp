#include "API/Semaphore.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    Semaphore::Semaphore(bool timeline, const std::string &name)
        : m_timeline{timeline},
          m_stageFlags{vk::PipelineStageFlagBits2::eNone}
    {
        vk::SemaphoreCreateInfo si{};
        vk::SemaphoreTypeCreateInfo ti{};
        if (timeline)
        {
            ti.sType = vk::StructureType::eSemaphoreTypeCreateInfo;
            ti.semaphoreType = vk::SemaphoreType::eTimeline;
            ti.initialValue = 0;
            si.pNext = &ti;
        }

        m_apiHandle = VulkanRhi::Device().createSemaphore(si);

        Debug::SetObjectName(m_apiHandle, name);
    }

    Semaphore::~Semaphore()
    {
        if (m_apiHandle)
            VulkanRhi::Device().destroySemaphore(m_apiHandle);
    }

    void Semaphore::Wait(uint64_t value)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::Wait() called on non-timeline semaphore!");

        if (value <= m_lastCompleted)
            return;

        vk::SemaphoreWaitInfo swi{};
        swi.semaphoreCount = 1;
        swi.pSemaphores = &m_apiHandle;
        swi.pValues = &value;

        auto result = VulkanRhi::Device().waitSemaphores(swi, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            if (result == vk::Result::eTimeout)
                PE_INFO("Timeout while waiting for timeline semaphore");
            else
                PE_ERROR("[Semaphore] Failed to wait for timeline semaphore!");
        }
        else
        {
            m_lastCompleted = value;
        }
    }

    bool Semaphore::WaitTimeout(uint64_t value, uint64_t timeoutNS)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::WaitTimeout() called on non-timeline semaphore!");

        if (value <= m_lastCompleted)
            return true;

        vk::SemaphoreWaitInfo swi{};
        swi.semaphoreCount = 1;
        swi.pSemaphores = &m_apiHandle;
        swi.pValues = &value;

        auto result = VulkanRhi::Device().waitSemaphores(swi, timeoutNS);
        if (result == vk::Result::eSuccess)
        {
            m_lastCompleted = value;
            return true;
        }

        return false;
    }

    void Semaphore::Signal(uint64_t value)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::Signal() called on non-timeline semaphore!");

        vk::SemaphoreSignalInfo ssi{};
        ssi.semaphore = m_apiHandle;
        ssi.value = value;

        VulkanRhi::Device().signalSemaphore(ssi);
    }

    uint64_t Semaphore::GetValue()
    {
        if (!m_timeline)
            return 0;

        return VulkanRhi::Device().getSemaphoreCounterValue(m_apiHandle);
    }
} // namespace pe
