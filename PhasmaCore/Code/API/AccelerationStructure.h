#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Buffer;
    class CommandBuffer;

    class AccelerationStructure : public PeHandle<AccelerationStructure, PeBackendHandle>
    {
    public:
        AccelerationStructure(const std::string &name, Buffer *buffer = nullptr, uint64_t offset = 0);
        ~AccelerationStructure();

        void BuildTLAS(CommandBuffer *cmd,
                       uint32_t instanceCount,
                       Buffer *instanceBuffer,
                       PeAccelerationStructureBuildFlags flags,
                       uint64_t scratchAddress = 0);

        void UpdateTLAS(CommandBuffer *cmd,
                        uint32_t instanceCount,
                        Buffer *instanceBuffer,
                        uint64_t scratchAddress = 0);

        uint64_t GetDeviceAddress() const;

    private:
        friend struct AccelerationStructureAccess;

        void CreateBuffer(size_t size);
        void CreateScratchBuffer(size_t size);

        Buffer *m_buffer = nullptr;
        uint64_t m_offset = 0;
        bool m_externalBuffer = false;
        Buffer *m_scratchBuffer = nullptr;
        std::string m_name;
        uint64_t m_deviceAddress = 0;
    };
} // namespace pe
