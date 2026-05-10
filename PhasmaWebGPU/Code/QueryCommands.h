#pragma once

#include <webgpu/webgpu.h>
#include "API/RHITypes.h"

namespace pe
{
    class CommandBuffer;
} // namespace pe

struct WGPUBufferImpl;
struct WGPUDeviceImpl;
struct WGPUQuerySetImpl;

namespace pwgpu
{
    struct QueryBackendResult
    {
        PeBackendHandle backendQueryPool = 0;
        WGPUErrorType errorType = WGPUErrorType_Internal;
        std::string message;

        bool Succeeded() const { return backendQueryPool != 0; }
    };

    QueryBackendResult CreateWebGPUQuerySetBackend(WGPUDeviceImpl *device,
                                                   WGPUQueryType type,
                                                   uint32_t count);
    void DestroyWebGPUQuerySetBackend(WGPUQuerySetImpl *querySet);

    bool ResetWebGPUQuerySet(pe::CommandBuffer *cmd,
                             WGPUQuerySetImpl *querySet,
                             uint32_t firstQuery,
                             uint32_t queryCount);
    bool BeginWebGPUOcclusionQuery(pe::CommandBuffer *cmd,
                                   WGPUQuerySetImpl *querySet,
                                   uint32_t queryIndex);
    bool EndWebGPUOcclusionQuery(pe::CommandBuffer *cmd,
                                 WGPUQuerySetImpl *querySet,
                                 uint32_t queryIndex);
    bool WriteWebGPUTimestamp(pe::CommandBuffer *cmd,
                              WGPUQuerySetImpl *querySet,
                              uint32_t queryIndex);
    bool ResolveWebGPUQuerySet(pe::CommandBuffer *cmd,
                               WGPUQuerySetImpl *querySet,
                               uint32_t firstQuery,
                               uint32_t queryCount,
                               WGPUBufferImpl *dst,
                               uint64_t dstOffset);
} // namespace pwgpu
