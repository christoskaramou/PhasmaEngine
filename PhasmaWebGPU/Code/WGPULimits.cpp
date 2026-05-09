#include "WGPULimits.h"
#include "API/RHI.h"
#include <algorithm>

namespace pwgpu
{

    void FillLimitsFromCore(WGPULimits &out, const pe::GpuLimits &limits)
    {
        out.nextInChain = nullptr;
        out.maxTextureDimension1D = limits.maxTextureDimension1D;
        out.maxTextureDimension2D = limits.maxTextureDimension2D;
        out.maxTextureDimension3D = limits.maxTextureDimension3D;
        out.maxTextureArrayLayers = limits.maxTextureArrayLayers;
        out.maxBindGroups = limits.maxBindGroups;
        out.maxDynamicUniformBuffersPerPipelineLayout = limits.maxDynamicUniformBuffersPerPipelineLayout;
        out.maxDynamicStorageBuffersPerPipelineLayout = limits.maxDynamicStorageBuffersPerPipelineLayout;
        out.maxSampledTexturesPerShaderStage = limits.maxSampledTexturesPerShaderStage;
        out.maxSamplersPerShaderStage = limits.maxSamplersPerShaderStage;
        out.maxStorageBuffersPerShaderStage = limits.maxStorageBuffersPerShaderStage;
        out.maxStorageTexturesPerShaderStage = limits.maxStorageTexturesPerShaderStage;
        out.maxUniformBuffersPerShaderStage = limits.maxUniformBuffersPerShaderStage;
        out.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
        out.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;
        out.minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
        out.minStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
        out.maxVertexBuffers = limits.maxVertexBuffers;
        out.maxBufferSize = limits.maxBufferSize;
        out.maxVertexAttributes = limits.maxVertexAttributes;
        out.maxVertexBufferArrayStride = limits.maxVertexBufferArrayStride;
        out.maxInterStageShaderVariables = limits.maxInterStageShaderVariables;
        out.maxColorAttachments = limits.maxColorAttachments;
        // RHI exposes backend limits; WebGPU derives this non-native aggregate.
        out.maxColorAttachmentBytesPerSample = 64;
        out.maxComputeWorkgroupStorageSize = limits.maxComputeWorkgroupStorageSize;
        out.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
        out.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
        out.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
        out.maxComputeWorkgroupSizeZ = limits.maxComputeWorkgroupSizeZ;
        out.maxComputeWorkgroupsPerDimension = limits.maxComputeWorkgroupsPerDimension;
        out.maxImmediateSize = 0;

        const uint32_t perStage = out.maxSampledTexturesPerShaderStage +
                                  out.maxSamplersPerShaderStage +
                                  out.maxStorageBuffersPerShaderStage +
                                  out.maxStorageTexturesPerShaderStage +
                                  out.maxUniformBuffersPerShaderStage;
        const WGPULimits defaults = DefaultLimits();
        out.maxBindingsPerBindGroup =
            std::max(perStage * 2u, defaults.maxBindingsPerBindGroup);

        out.maxBindGroupsPlusVertexBuffers =
            std::max(out.maxBindGroups + out.maxVertexBuffers,
                     defaults.maxBindGroupsPlusVertexBuffers);
    }

    namespace
    {

        // Zero is treated as "unset" alongside the sentinel so zero-initialized
        // WGPULimits work out of the box (matches dawn/wgpu-native).
        template <typename T>
        inline bool IsUndefinedU(T v);

        template <>
        inline bool IsUndefinedU<uint32_t>(uint32_t v)
        {
            return v == WGPU_LIMIT_U32_UNDEFINED || v == 0u;
        }
        template <>
        inline bool IsUndefinedU<uint64_t>(uint64_t v)
        {
            return v == WGPU_LIMIT_U64_UNDEFINED || v == 0ull;
        }

        template <typename T>
        bool MaximumOk(T req, T adapter)
        {
            if (IsUndefinedU(req))
                return true;
            return req <= adapter;
        }

        template <typename T>
        bool AlignmentOk(T req, T adapter)
        {
            if (IsUndefinedU(req))
                return true;
            // Alignment limits are lower-is-better: must be power-of-two and ≥ adapter minimum.
            if ((req & (req - 1)) != 0)
                return false;
            return req >= adapter;
        }

        template <typename T>
        T ResolveMaximum(T req, T /*adapter*/, T defaultLim)
        {
            if (IsUndefinedU(req))
                return defaultLim;
            return (req > defaultLim) ? req : defaultLim;
        }

        template <typename T>
        T ResolveAlignment(T req, T /*adapter*/, T defaultLim)
        {
            if (IsUndefinedU(req))
                return defaultLim;
            if ((req & (req - 1)) != 0)
                return defaultLim;
            return (req < defaultLim) ? req : defaultLim;
        }

    } // namespace

#define PWGPU_CHECK_MAX(field)                             \
    do                                                     \
    {                                                      \
        if (!MaximumOk(requested.field, adapterLim.field)) \
        {                                                  \
            outFirstBadLimit = #field;                     \
            return WGPUStatus_Error;                       \
        }                                                  \
    }                                                      \
    while (0)

#define PWGPU_CHECK_ALIGN(field)                             \
    do                                                       \
    {                                                        \
        if (!AlignmentOk(requested.field, adapterLim.field)) \
        {                                                    \
            outFirstBadLimit = #field;                       \
            return WGPUStatus_Error;                         \
        }                                                    \
    }                                                        \
    while (0)

    WGPUStatus ValidateRequestedLimits(const WGPULimits &adapterLim,
                                       const WGPULimits &requested,
                                       std::string &outFirstBadLimit)
    {
        PWGPU_CHECK_MAX(maxTextureDimension1D);
        PWGPU_CHECK_MAX(maxTextureDimension2D);
        PWGPU_CHECK_MAX(maxTextureDimension3D);
        PWGPU_CHECK_MAX(maxTextureArrayLayers);
        PWGPU_CHECK_MAX(maxBindGroups);
        PWGPU_CHECK_MAX(maxBindGroupsPlusVertexBuffers);
        PWGPU_CHECK_MAX(maxBindingsPerBindGroup);
        PWGPU_CHECK_MAX(maxDynamicUniformBuffersPerPipelineLayout);
        PWGPU_CHECK_MAX(maxDynamicStorageBuffersPerPipelineLayout);
        PWGPU_CHECK_MAX(maxSampledTexturesPerShaderStage);
        PWGPU_CHECK_MAX(maxSamplersPerShaderStage);
        PWGPU_CHECK_MAX(maxStorageBuffersPerShaderStage);
        PWGPU_CHECK_MAX(maxStorageTexturesPerShaderStage);
        PWGPU_CHECK_MAX(maxUniformBuffersPerShaderStage);
        PWGPU_CHECK_MAX(maxUniformBufferBindingSize);
        PWGPU_CHECK_MAX(maxStorageBufferBindingSize);
        PWGPU_CHECK_ALIGN(minUniformBufferOffsetAlignment);
        PWGPU_CHECK_ALIGN(minStorageBufferOffsetAlignment);
        PWGPU_CHECK_MAX(maxVertexBuffers);
        PWGPU_CHECK_MAX(maxBufferSize);
        PWGPU_CHECK_MAX(maxVertexAttributes);
        PWGPU_CHECK_MAX(maxVertexBufferArrayStride);
        PWGPU_CHECK_MAX(maxInterStageShaderVariables);
        PWGPU_CHECK_MAX(maxColorAttachments);
        PWGPU_CHECK_MAX(maxColorAttachmentBytesPerSample);
        PWGPU_CHECK_MAX(maxComputeWorkgroupStorageSize);
        PWGPU_CHECK_MAX(maxComputeInvocationsPerWorkgroup);
        PWGPU_CHECK_MAX(maxComputeWorkgroupSizeX);
        PWGPU_CHECK_MAX(maxComputeWorkgroupSizeY);
        PWGPU_CHECK_MAX(maxComputeWorkgroupSizeZ);
        PWGPU_CHECK_MAX(maxComputeWorkgroupsPerDimension);
        PWGPU_CHECK_MAX(maxImmediateSize);
        return WGPUStatus_Success;
    }

#undef PWGPU_CHECK_MAX
#undef PWGPU_CHECK_ALIGN

    namespace
    {
        template <typename T>
        bool IsPow2(T v)
        {
            return v != 0 && (v & (v - 1)) == 0;
        }
    } // namespace

#define PWGPU_ADAPTER_MIN(field)          \
    do                                    \
    {                                     \
        if (adapterLim.field < def.field) \
        {                                 \
            outFirstBadLimit = #field;    \
            return WGPUStatus_Error;      \
        }                                 \
    }                                     \
    while (0)

#define PWGPU_ADAPTER_ALIGN(field)          \
    do                                      \
    {                                       \
        if (adapterLim.field > def.field || \
            !IsPow2(adapterLim.field))      \
        {                                   \
            outFirstBadLimit = #field;      \
            return WGPUStatus_Error;        \
        }                                   \
    }                                       \
    while (0)

    WGPUStatus ValidateAdapterLimits(const WGPULimits &adapterLim,
                                     std::string &outFirstBadLimit)
    {
        const WGPULimits def = DefaultLimits();
        PWGPU_ADAPTER_MIN(maxTextureDimension1D);
        PWGPU_ADAPTER_MIN(maxTextureDimension2D);
        PWGPU_ADAPTER_MIN(maxTextureDimension3D);
        PWGPU_ADAPTER_MIN(maxTextureArrayLayers);
        PWGPU_ADAPTER_MIN(maxBindGroups);
        PWGPU_ADAPTER_MIN(maxBindGroupsPlusVertexBuffers);
        PWGPU_ADAPTER_MIN(maxBindingsPerBindGroup);
        PWGPU_ADAPTER_MIN(maxDynamicUniformBuffersPerPipelineLayout);
        PWGPU_ADAPTER_MIN(maxDynamicStorageBuffersPerPipelineLayout);
        PWGPU_ADAPTER_MIN(maxSampledTexturesPerShaderStage);
        PWGPU_ADAPTER_MIN(maxSamplersPerShaderStage);
        PWGPU_ADAPTER_MIN(maxStorageBuffersPerShaderStage);
        PWGPU_ADAPTER_MIN(maxStorageTexturesPerShaderStage);
        PWGPU_ADAPTER_MIN(maxUniformBuffersPerShaderStage);
        PWGPU_ADAPTER_MIN(maxUniformBufferBindingSize);
        PWGPU_ADAPTER_MIN(maxStorageBufferBindingSize);
        PWGPU_ADAPTER_ALIGN(minUniformBufferOffsetAlignment);
        PWGPU_ADAPTER_ALIGN(minStorageBufferOffsetAlignment);
        PWGPU_ADAPTER_MIN(maxVertexBuffers);
        PWGPU_ADAPTER_MIN(maxBufferSize);
        PWGPU_ADAPTER_MIN(maxVertexAttributes);
        PWGPU_ADAPTER_MIN(maxVertexBufferArrayStride);
        PWGPU_ADAPTER_MIN(maxInterStageShaderVariables);
        PWGPU_ADAPTER_MIN(maxColorAttachments);
        PWGPU_ADAPTER_MIN(maxColorAttachmentBytesPerSample);
        PWGPU_ADAPTER_MIN(maxComputeWorkgroupStorageSize);
        PWGPU_ADAPTER_MIN(maxComputeInvocationsPerWorkgroup);
        PWGPU_ADAPTER_MIN(maxComputeWorkgroupSizeX);
        PWGPU_ADAPTER_MIN(maxComputeWorkgroupSizeY);
        PWGPU_ADAPTER_MIN(maxComputeWorkgroupSizeZ);
        PWGPU_ADAPTER_MIN(maxComputeWorkgroupsPerDimension);
        return WGPUStatus_Success;
    }

#undef PWGPU_ADAPTER_MIN
#undef PWGPU_ADAPTER_ALIGN

#define PWGPU_RESOLVE_MAX(field) \
    out.field = ResolveMaximum(req.field, adapterLim.field, defaults.field)
#define PWGPU_RESOLVE_ALIGN(field) \
    out.field = ResolveAlignment(req.field, adapterLim.field, defaults.field)

    WGPULimits ResolveDeviceLimits(const WGPULimits &adapterLim, const WGPULimits *requested)
    {
        const WGPULimits defaults = DefaultLimits();
        if (!requested)
            return defaults;

        WGPULimits out{};
        const WGPULimits &req = *requested;
        PWGPU_RESOLVE_MAX(maxTextureDimension1D);
        PWGPU_RESOLVE_MAX(maxTextureDimension2D);
        PWGPU_RESOLVE_MAX(maxTextureDimension3D);
        PWGPU_RESOLVE_MAX(maxTextureArrayLayers);
        PWGPU_RESOLVE_MAX(maxBindGroups);
        PWGPU_RESOLVE_MAX(maxBindGroupsPlusVertexBuffers);
        PWGPU_RESOLVE_MAX(maxBindingsPerBindGroup);
        PWGPU_RESOLVE_MAX(maxDynamicUniformBuffersPerPipelineLayout);
        PWGPU_RESOLVE_MAX(maxDynamicStorageBuffersPerPipelineLayout);
        PWGPU_RESOLVE_MAX(maxSampledTexturesPerShaderStage);
        PWGPU_RESOLVE_MAX(maxSamplersPerShaderStage);
        PWGPU_RESOLVE_MAX(maxStorageBuffersPerShaderStage);
        PWGPU_RESOLVE_MAX(maxStorageTexturesPerShaderStage);
        PWGPU_RESOLVE_MAX(maxUniformBuffersPerShaderStage);
        PWGPU_RESOLVE_MAX(maxUniformBufferBindingSize);
        PWGPU_RESOLVE_MAX(maxStorageBufferBindingSize);
        PWGPU_RESOLVE_ALIGN(minUniformBufferOffsetAlignment);
        PWGPU_RESOLVE_ALIGN(minStorageBufferOffsetAlignment);
        PWGPU_RESOLVE_MAX(maxVertexBuffers);
        PWGPU_RESOLVE_MAX(maxBufferSize);
        PWGPU_RESOLVE_MAX(maxVertexAttributes);
        PWGPU_RESOLVE_MAX(maxVertexBufferArrayStride);
        PWGPU_RESOLVE_MAX(maxInterStageShaderVariables);
        PWGPU_RESOLVE_MAX(maxColorAttachments);
        PWGPU_RESOLVE_MAX(maxColorAttachmentBytesPerSample);
        PWGPU_RESOLVE_MAX(maxComputeWorkgroupStorageSize);
        PWGPU_RESOLVE_MAX(maxComputeInvocationsPerWorkgroup);
        PWGPU_RESOLVE_MAX(maxComputeWorkgroupSizeX);
        PWGPU_RESOLVE_MAX(maxComputeWorkgroupSizeY);
        PWGPU_RESOLVE_MAX(maxComputeWorkgroupSizeZ);
        PWGPU_RESOLVE_MAX(maxComputeWorkgroupsPerDimension);
        PWGPU_RESOLVE_MAX(maxImmediateSize);
        out.nextInChain = nullptr;
        return out;
    }

#undef PWGPU_RESOLVE_MAX
#undef PWGPU_RESOLVE_ALIGN

} // namespace pwgpu
