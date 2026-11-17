#include "WGPULimits.h"
#include "API/RHI.h"
#include "PipelineBinding.h"

namespace pwgpu
{

    void FillLimitsFromCore(WGPULimits &out, const pe::GpuLimits &limits, PeGraphicsApi api)
    {
        out.nextInChain = nullptr;

        // Capped at Dawn's highest limit tiers (src/dawn/native/Limits.cpp), the values Chrome reports on
        // high-end GPUs. Raw Vulkan counts such as 1,048,576 textures per stage are not usable through WebGPU,
        // and the CTS maximum-limit tests build shaders and layouts that large.
        // ponytail: one fixed tier for every adapter; bucket by hardware like Dawn if lower tiers are needed.
        // With maxBindGroups 4 and maxVertexBuffers 8, maxBindGroupsPlusVertexBuffers (24) cannot be exceeded,
        // so its createRenderPipeline and draw checks are not implemented; add them if these caps rise.
        constexpr uint64_t kMaxBufferBytes = 4ull * 1024 * 1024 * 1024 - 4;
        out.maxTextureDimension1D = std::min(limits.maxTextureDimension1D, 16384u);
        out.maxTextureDimension2D = std::min(limits.maxTextureDimension2D, 16384u);
        out.maxTextureDimension3D = std::min(limits.maxTextureDimension3D, 2048u);
        out.maxTextureArrayLayers = std::min(limits.maxTextureArrayLayers, 2048u);
        out.maxBindGroups = std::min(limits.maxBindGroups, 4u);
        out.maxDynamicUniformBuffersPerPipelineLayout = std::min(limits.maxDynamicUniformBuffersPerPipelineLayout, 10u);
        out.maxDynamicStorageBuffersPerPipelineLayout = std::min(limits.maxDynamicStorageBuffersPerPipelineLayout, 8u);
        out.maxSampledTexturesPerShaderStage = std::min(limits.maxSampledTexturesPerShaderStage, 48u);
        out.maxSamplersPerShaderStage = std::min(limits.maxSamplersPerShaderStage, 16u);
        out.maxStorageBuffersPerShaderStage = std::min(limits.maxStorageBuffersPerShaderStage, 16u);
        out.maxStorageTexturesPerShaderStage = std::min(limits.maxStorageTexturesPerShaderStage, 8u);
        out.maxUniformBuffersPerShaderStage = std::min(limits.maxUniformBuffersPerShaderStage, 12u);

        // pe::Buffer rounds a uniform buffer up to the backend's uniform alignment and rejects one above the
        // backend maximum, so the limit is rounded down to that alignment: the Intel UHD 770 reports
        // 134,217,724 with an alignment of 64, and a buffer of exactly that size failed to allocate.
        const uint64_t uniformAlignment = std::max(limits.minUniformBufferOffsetAlignment, 1u);
        out.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize & ~(uniformAlignment - 1);

        // Adapter capability guarantees: maxStorageBufferBindingSize is a multiple of 4 and both offset
        // alignments are at least 32 bytes (Vulkan reports 2^32-1 and 16 on NVIDIA).
        // pe::Buffer rounds storage sizes up to the backend's storage alignment, so the binding size is rounded
        // down to it: a buffer of exactly this size must still fit the range (2^32-4 would round up to 2^32).
        const uint64_t storageAlignment = std::max(limits.minStorageBufferOffsetAlignment, 4u);
        const uint64_t maxStorageBinding = std::min<uint64_t>(limits.maxStorageBufferBindingSize, kMaxBufferBytes);
        out.maxStorageBufferBindingSize = maxStorageBinding & ~(storageAlignment - 1);
        out.minUniformBufferOffsetAlignment = std::max(limits.minUniformBufferOffsetAlignment, 32u);
        out.minStorageBufferOffsetAlignment = std::max(limits.minStorageBufferOffsetAlignment, 32u);
        out.maxVertexBuffers = std::min(limits.maxVertexBuffers, 8u);
        out.maxBufferSize = std::min<uint64_t>(limits.maxBufferSize, kMaxBufferBytes);
        out.maxVertexAttributes = std::min(limits.maxVertexAttributes, 30u);

        // arrayStride must be a multiple of 4, so the limit is one too (the Intel UHD 770 reports 4095).
        out.maxVertexBufferArrayStride = limits.maxVertexBufferArrayStride & ~3u;
        out.maxInterStageShaderVariables = std::min(limits.maxInterStageShaderVariables, 28u);
        out.maxColorAttachments = limits.maxColorAttachments;

        // RHI exposes backend limits; WebGPU derives this non-native aggregate.
        out.maxColorAttachmentBytesPerSample = 64;
        out.maxComputeWorkgroupStorageSize = std::min(limits.maxComputeWorkgroupStorageSize, 65536u);
        out.maxComputeInvocationsPerWorkgroup = std::min(limits.maxComputeInvocationsPerWorkgroup, 1024u);
        out.maxComputeWorkgroupSizeX = std::min(limits.maxComputeWorkgroupSizeX, 1024u);
        out.maxComputeWorkgroupSizeY = std::min(limits.maxComputeWorkgroupSizeY, 1024u);
        out.maxComputeWorkgroupSizeZ = std::min(limits.maxComputeWorkgroupSizeZ, 64u);
        out.maxComputeWorkgroupsPerDimension = limits.maxComputeWorkgroupsPerDimension;

        // ponytail: immediates are Vulkan push constants; DX12 needs a root-constant path before it can expose them.
        out.maxImmediateSize = api == PE_GRAPHICS_API_VULKAN ? WebGPUImmediates::kMaxSize : 0;

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
        {
            WGPULimits out = defaults;
            out.maxImmediateSize = std::min(out.maxImmediateSize, adapterLim.maxImmediateSize);
            return out;
        }

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

        // The one default an adapter may fall short of (see FillLimitsFromCore).
        out.maxImmediateSize = std::min(out.maxImmediateSize, adapterLim.maxImmediateSize);
        out.nextInChain = nullptr;
        return out;
    }

#undef PWGPU_RESOLVE_MAX
#undef PWGPU_RESOLVE_ALIGN

} // namespace pwgpu
