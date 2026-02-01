#pragma once

namespace pe
{
    class CommandBuffer;
    class Buffer;

    class AccelerationStructure : public PeHandle<AccelerationStructure, vk::AccelerationStructureKHR>
    {
    public:
        static vk::AccelerationStructureBuildSizesInfoKHR GetBuildSizes(
            const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
            const std::vector<uint32_t> &maxPrimitiveCounts,
            vk::AccelerationStructureTypeKHR accelerationStructureType,
            vk::BuildAccelerationStructureFlagsKHR flags,
            vk::AccelerationStructureBuildTypeKHR type);

        AccelerationStructure(const std::string &name, Buffer *buffer = nullptr, uint64_t offset = 0);
        ~AccelerationStructure();

        // BLAS
        void BuildBLAS(CommandBuffer *cmd,
                       const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
                       const std::vector<vk::AccelerationStructureBuildRangeInfoKHR> &buildRanges,
                       const std::vector<uint32_t> &maxPrimitiveCounts,
                       vk::BuildAccelerationStructureFlagsKHR flags,
                       vk::DeviceAddress scratchAddress = 0);

        // TLAS
        void BuildTLAS(CommandBuffer *cmd,
                       uint32_t instanceCount,
                       Buffer *instanceBuffer,
                       vk::BuildAccelerationStructureFlagsKHR flags,
                       vk::DeviceAddress scratchAddress = 0);

        void UpdateTLAS(CommandBuffer *cmd,
                        uint32_t instanceCount,
                        Buffer *instanceBuffer,
                        vk::DeviceAddress scratchAddress = 0);

        uint64_t GetDeviceAddress() const;

    private:
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
