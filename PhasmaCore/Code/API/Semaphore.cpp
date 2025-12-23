#include "API/Semaphore.h"
#include "API/RHI.h"

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

        m_apiHandle = RHII.GetDevice().createSemaphore(si);

        Debug::SetObjectName(m_apiHandle, name);
    }

    Semaphore::~Semaphore()
    {
        if (m_apiHandle)
            RHII.GetDevice().destroySemaphore(m_apiHandle);
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

        auto result = RHII.GetDevice().waitSemaphores(swi, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            if (result == vk::Result::eTimeout)
                PE_INFO("Timeout while waiting for timeline semaphore");
            else
                PE_ERROR("Failed to wait for timeline semaphore!");
        }
        else
        {
            m_lastCompleted = value;
        }
    }

    void Semaphore::Signal(uint64_t value)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::Signal() called on non-timeline semaphore!");

        vk::SemaphoreSignalInfo ssi{};
        ssi.semaphore = m_apiHandle;
        ssi.value = value;

        RHII.GetDevice().signalSemaphore(ssi);
    }

    uint64_t Semaphore::GetValue()
    {
        if (!m_timeline)
            return 0;

        return RHII.GetDevice().getSemaphoreCounterValue(m_apiHandle);
    }
} // namespace pe
