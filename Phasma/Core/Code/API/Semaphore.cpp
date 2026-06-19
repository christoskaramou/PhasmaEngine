#include "API/Semaphore_Internal.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanSemaphoreImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12SemaphoreImpl.h"
#endif

namespace pe
{
    Semaphore::Impl *CreateSemaphoreImpl(Semaphore *owner, bool timeline, const std::string &name)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            return new Dx12SemaphoreImpl(owner, timeline, name);
#else
            PE_ERROR("CreateSemaphoreImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        }
        return new VulkanSemaphoreImpl(owner, timeline, name);
    }

    Semaphore::Semaphore(bool timeline, const std::string &name)
        : m_timeline{timeline},
          m_stageFlags{PE_STAGE_NONE}
    {
        m_impl = CreateSemaphoreImpl(this, timeline, name);
    }

    Semaphore::~Semaphore()
    {
        delete m_impl;
        m_impl = nullptr;
    }

    void Semaphore::Wait(uint64_t value)
    {
        m_impl->Wait(value);
    }

    bool Semaphore::WaitTimeout(uint64_t value, uint64_t timeoutNS)
    {
        return m_impl->WaitTimeout(value, timeoutNS);
    }

    void Semaphore::Signal(uint64_t value)
    {
        m_impl->Signal(value);
    }

    uint64_t Semaphore::GetValue()
    {
        return m_impl->GetValue();
    }
} // namespace pe
