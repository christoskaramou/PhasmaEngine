#include "API/AccelerationStructure.h"

#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Vulkan/AccelerationStructure.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12AccelerationStructure.h"
#endif

namespace pe
{
    AccelerationStructure::AccelerationStructure(const std::string &name, Buffer *buffer, uint64_t offset)
        : m_buffer(buffer), m_offset(offset), m_name(name)
    {
        if (m_buffer)
            m_externalBuffer = true;
    }

    AccelerationStructure::~AccelerationStructure()
    {
        if (m_apiHandle && RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
        {
            DestroyVulkanAccelerationStructure(this);
        }
        else if (m_apiHandle)
        {
            PE_WARN("AccelerationStructure destroyed with backend handle for unsupported API %u",
                    static_cast<uint32_t>(RHII.GetApi()));
        }

        if (!m_externalBuffer)
            Buffer::Destroy(m_buffer);
        Buffer::Destroy(m_scratchBuffer);
    }

    void AccelerationStructure::CreateBuffer(size_t size)
    {
        m_buffer = Buffer::Create({
            .size = size,
            .usage = PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = m_name + "_buffer",
        });
    }

    void AccelerationStructure::CreateScratchBuffer(size_t size)
    {
        m_scratchBuffer = Buffer::Create({
            .size = size,
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = m_name + "_scratch_buffer",
        });
    }

    void AccelerationStructure::BuildTLAS(CommandBuffer *cmd,
                                          uint32_t instanceCount,
                                          Buffer *instanceBuffer,
                                          PeAccelerationStructureBuildFlags flags,
                                          uint64_t scratchAddress)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            BuildVulkanTLAS(this, cmd, instanceCount, instanceBuffer, flags, scratchAddress);
            return;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            BuildDx12TLAS(this, cmd, instanceCount, instanceBuffer, flags, scratchAddress);
#else
            PE_ERROR("DX12 TLAS build requested on a non-Windows build");
#endif
            return;
        default:
            PE_ERROR("AccelerationStructure::BuildTLAS requested for unsupported API %u",
                     static_cast<uint32_t>(RHII.GetApi()));
            return;
        }
    }

    void AccelerationStructure::UpdateTLAS(CommandBuffer *cmd,
                                           uint32_t instanceCount,
                                           Buffer *instanceBuffer,
                                           uint64_t scratchAddress)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            UpdateVulkanTLAS(this, cmd, instanceCount, instanceBuffer, scratchAddress);
            return;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            UpdateDx12TLAS(this, cmd, instanceCount, instanceBuffer, scratchAddress);
#else
            PE_ERROR("DX12 TLAS update requested on a non-Windows build");
#endif
            return;
        default:
            PE_ERROR("AccelerationStructure::UpdateTLAS requested for unsupported API %u",
                     static_cast<uint32_t>(RHII.GetApi()));
            return;
        }
    }

    uint64_t AccelerationStructure::GetDeviceAddress() const
    {
        return m_deviceAddress;
    }
} // namespace pe
