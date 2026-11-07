#include "Base/Timer_Internal.h"

#include "API/RHI.h"
#if defined(PE_VULKAN)
#include "API/Vulkan/VulkanGpuTimerImpl.h"
#endif

#if defined(PE_WIN32)
#include "API/DX12/Dx12GpuTimerImpl.h"
#endif

namespace pe
{
    std::unique_ptr<GpuTimer::Impl> CreateGpuTimerImpl(const std::string &name)
    {
        switch (RHII.GetApi())
        {
#if defined(PE_VULKAN)
        case PE_GRAPHICS_API_VULKAN:
            return std::make_unique<VulkanGpuTimerImpl>(name);
#endif
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            return std::make_unique<Dx12GpuTimerImpl>(name);
#else
            PE_ERROR("CreateGpuTimerImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        default:
            PE_ERROR("CreateGpuTimerImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }
} // namespace pe
