#pragma once

#include "API/AccelerationStructure.h"

#if defined(PE_WIN32)

#include <d3d12.h>


namespace pe
{
    struct Dx12AccelerationStructureAccess
    {
        static void CreateBuffer(AccelerationStructure *as, size_t size) { as->CreateBuffer(size); }
        static void CreateScratchBuffer(AccelerationStructure *as, size_t size) { as->CreateScratchBuffer(size); }
        static Buffer *GetBuffer(AccelerationStructure *as) { return as->m_buffer; }
        static Buffer *GetScratchBuffer(AccelerationStructure *as) { return as->m_scratchBuffer; }
        static uint64_t GetOffset(AccelerationStructure *as) { return as->m_offset; }
        static bool IsExternalBuffer(AccelerationStructure *as) { return as->m_externalBuffer; }
        static void SetDeviceAddress(AccelerationStructure *as, uint64_t address) { as->m_deviceAddress = address; }
    };

    struct Dx12RtTriangleGeometry
    {
        uint64_t vertexAddress = 0;
        uint32_t vertexCount = 0;
        uint32_t vertexStride = 0;
        ::PeFormat vertexFormat = PE_FORMAT_UNDEFINED;
        uint64_t indexAddress = 0;
        uint32_t indexCount = 0;
        PeIndexType indexType = PE_INDEX_TYPE_UINT32;
        uint64_t transformAddress = 0;
        bool opaque = true;
    };

    struct Dx12RtPrebuildInfo
    {
        uint64_t resultDataMaxSize = 0;
        uint64_t buildScratchSize = 0;
        uint64_t updateScratchSize = 0;
    };

    Dx12RtPrebuildInfo GetDx12BlasPrebuildInfo(const std::vector<Dx12RtTriangleGeometry> &geometries,
                                               PeAccelerationStructureBuildFlags flags);
    Dx12RtPrebuildInfo GetDx12TlasPrebuildInfo(uint32_t instanceCount,
                                               PeAccelerationStructureBuildFlags flags);

    void BuildDx12BLAS(AccelerationStructure *as,
                       CommandBuffer *cmd,
                       const std::vector<Dx12RtTriangleGeometry> &geometries,
                       PeAccelerationStructureBuildFlags flags,
                       uint64_t scratchAddress = 0);
    void BuildDx12TLAS(AccelerationStructure *as,
                       CommandBuffer *cmd,
                       uint32_t instanceCount,
                       Buffer *instanceBuffer,
                       PeAccelerationStructureBuildFlags flags,
                       uint64_t scratchAddress = 0);
    void UpdateDx12TLAS(AccelerationStructure *as,
                        CommandBuffer *cmd,
                        uint32_t instanceCount,
                        Buffer *instanceBuffer,
                        uint64_t scratchAddress = 0);
} // namespace pe

#endif // PE_WIN32
