#include "Reflect.h"
#include "BindGroup.h"
#include "PipelineLayout.h"
#include "Device.h"
#include "API/RHI.h"
#include "API/Descriptor.h"

#include "spirv_cross/spirv_cross.hpp"

#include <map>
#include <set>
#include <vector>

namespace pwgpu
{
    namespace
    {
        // Per-binding reflected info from one stage. We merge across stages into
        // WGPUBindGroupLayoutEntryResolved with OR'd visibility.
        struct ReflectedBinding
        {
            uint32_t set = 0;
            uint32_t binding = 0;
            WGPUShaderStage visibility = WGPUShaderStage_None;

            bool hasBuffer = false;
            WGPUBufferBindingLayout buffer{};
            bool hasSampler = false;
            WGPUSamplerBindingLayout sampler{};
            bool hasTexture = false;
            WGPUTextureBindingLayout texture{};
            bool hasStorageTexture = false;
            WGPUStorageTextureBindingLayout storageTexture{};
        };

        WGPUTextureViewDimension DimensionFromSpirv(const spirv_cross::SPIRType &t)
        {
            switch (t.image.dim)
            {
            case spv::Dim1D:
                return WGPUTextureViewDimension_1D;
            case spv::Dim2D:
                if (t.image.arrayed)
                    return WGPUTextureViewDimension_2DArray;
                return WGPUTextureViewDimension_2D;
            case spv::Dim3D:
                return WGPUTextureViewDimension_3D;
            case spv::DimCube:
                if (t.image.arrayed)
                    return WGPUTextureViewDimension_CubeArray;
                return WGPUTextureViewDimension_Cube;
            default:
                return WGPUTextureViewDimension_2D;
            }
        }

        WGPUTextureSampleType SampleTypeFromSpirv(const spirv_cross::Compiler &c,
                                                  const spirv_cross::SPIRType &t)
        {
            if (t.image.depth)
                return WGPUTextureSampleType_Depth;
            const auto &sampled = c.get_type(t.image.type);
            switch (sampled.basetype)
            {
            case spirv_cross::SPIRType::Int:
            case spirv_cross::SPIRType::Short:
            case spirv_cross::SPIRType::SByte:
            case spirv_cross::SPIRType::Int64:
                return WGPUTextureSampleType_Sint;
            case spirv_cross::SPIRType::UInt:
            case spirv_cross::SPIRType::UShort:
            case spirv_cross::SPIRType::UByte:
            case spirv_cross::SPIRType::UInt64:
                return WGPUTextureSampleType_Uint;
            default:
                return WGPUTextureSampleType_Float;
            }
        }

        WGPUTextureFormat StorageFormatFromSpirv(spv::ImageFormat fmt)
        {
            switch (fmt)
            {
            case spv::ImageFormatRgba8:
                return WGPUTextureFormat_RGBA8Unorm;
            case spv::ImageFormatRgba8Snorm:
                return WGPUTextureFormat_RGBA8Snorm;
            case spv::ImageFormatRgba8ui:
                return WGPUTextureFormat_RGBA8Uint;
            case spv::ImageFormatRgba8i:
                return WGPUTextureFormat_RGBA8Sint;
            case spv::ImageFormatRgba16:
                return WGPUTextureFormat_RGBA16Unorm;
            case spv::ImageFormatRgba16Snorm:
                return WGPUTextureFormat_RGBA16Snorm;
            case spv::ImageFormatRgba16f:
                return WGPUTextureFormat_RGBA16Float;
            case spv::ImageFormatRgba16ui:
                return WGPUTextureFormat_RGBA16Uint;
            case spv::ImageFormatRgba16i:
                return WGPUTextureFormat_RGBA16Sint;
            case spv::ImageFormatRgba32f:
                return WGPUTextureFormat_RGBA32Float;
            case spv::ImageFormatRgba32ui:
                return WGPUTextureFormat_RGBA32Uint;
            case spv::ImageFormatRgba32i:
                return WGPUTextureFormat_RGBA32Sint;
            case spv::ImageFormatRg8:
                return WGPUTextureFormat_RG8Unorm;
            case spv::ImageFormatRg8Snorm:
                return WGPUTextureFormat_RG8Snorm;
            case spv::ImageFormatRg8ui:
                return WGPUTextureFormat_RG8Uint;
            case spv::ImageFormatRg8i:
                return WGPUTextureFormat_RG8Sint;
            case spv::ImageFormatRg16:
                return WGPUTextureFormat_RG16Unorm;
            case spv::ImageFormatRg16Snorm:
                return WGPUTextureFormat_RG16Snorm;
            case spv::ImageFormatRg16f:
                return WGPUTextureFormat_RG16Float;
            case spv::ImageFormatRg16ui:
                return WGPUTextureFormat_RG16Uint;
            case spv::ImageFormatRg16i:
                return WGPUTextureFormat_RG16Sint;
            case spv::ImageFormatRg32f:
                return WGPUTextureFormat_RG32Float;
            case spv::ImageFormatRg32ui:
                return WGPUTextureFormat_RG32Uint;
            case spv::ImageFormatRg32i:
                return WGPUTextureFormat_RG32Sint;
            case spv::ImageFormatR8:
                return WGPUTextureFormat_R8Unorm;
            case spv::ImageFormatR8Snorm:
                return WGPUTextureFormat_R8Snorm;
            case spv::ImageFormatR8ui:
                return WGPUTextureFormat_R8Uint;
            case spv::ImageFormatR8i:
                return WGPUTextureFormat_R8Sint;
            case spv::ImageFormatR16:
                return WGPUTextureFormat_R16Unorm;
            case spv::ImageFormatR16Snorm:
                return WGPUTextureFormat_R16Snorm;
            case spv::ImageFormatR16f:
                return WGPUTextureFormat_R16Float;
            case spv::ImageFormatR16ui:
                return WGPUTextureFormat_R16Uint;
            case spv::ImageFormatR16i:
                return WGPUTextureFormat_R16Sint;
            case spv::ImageFormatR32f:
                return WGPUTextureFormat_R32Float;
            case spv::ImageFormatR32ui:
                return WGPUTextureFormat_R32Uint;
            case spv::ImageFormatR32i:
                return WGPUTextureFormat_R32Sint;
            case spv::ImageFormatRgb10A2:
                return WGPUTextureFormat_RGB10A2Unorm;
            case spv::ImageFormatRgb10a2ui:
                return WGPUTextureFormat_RGB10A2Uint;
            case spv::ImageFormatR11fG11fB10f:
                return WGPUTextureFormat_RG11B10Ufloat;
            default:
                return WGPUTextureFormat_Undefined;
            }
        }

        bool ReflectOneStage(const AutoLayoutStageInput &stage,
                             std::map<uint32_t, std::map<uint32_t, ReflectedBinding>> &merged,
                             std::string &errMsg)
        {
            if (!stage.spirv || stage.spirv->empty())
            {
                errMsg = "auto-layout: shader module has no SPIR-V";
                return false;
            }
            spirv_cross::Compiler compiler{stage.spirv->data(), stage.spirv->size()};
            if (!stage.entryPoint.empty())
            {
                compiler.set_entry_point(stage.entryPoint,
                                         static_cast<spv::ExecutionModel>(stage.executionModel));
            }
            auto active = compiler.get_active_interface_variables();
            spirv_cross::ShaderResources res = compiler.get_shader_resources(active);

            auto mergeInto = [&](uint32_t set, uint32_t binding,
                                 const ReflectedBinding &incoming) -> bool
            {
                auto &slot = merged[set][binding];
                if (slot.visibility == WGPUShaderStage_None)
                {
                    slot = incoming;
                    return true;
                }
                if (slot.hasBuffer != incoming.hasBuffer ||
                    slot.hasSampler != incoming.hasSampler ||
                    slot.hasTexture != incoming.hasTexture ||
                    slot.hasStorageTexture != incoming.hasStorageTexture)
                {
                    errMsg = "auto-layout: binding type conflict across stages";
                    return false;
                }
                slot.visibility = static_cast<WGPUShaderStage>(slot.visibility | incoming.visibility);
                return true;
            };

            auto buildBase = [&](uint32_t set, uint32_t binding) -> ReflectedBinding
            {
                ReflectedBinding rb{};
                rb.set = set;
                rb.binding = binding;
                rb.visibility = stage.visibility;
                return rb;
            };

            // Uniform buffers
            for (const auto &r : res.uniform_buffers)
            {
                uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                ReflectedBinding rb = buildBase(set, bind);
                rb.hasBuffer = true;
                rb.buffer.type = WGPUBufferBindingType_Uniform;
                if (!mergeInto(set, bind, rb))
                    return false;
            }

            // Storage buffers (distinguish read-only via NonWritable decoration)
            for (const auto &r : res.storage_buffers)
            {
                uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                spirv_cross::Bitset flags = compiler.get_buffer_block_flags(r.id);
                bool readOnly = flags.get(spv::DecorationNonWritable);
                ReflectedBinding rb = buildBase(set, bind);
                rb.hasBuffer = true;
                rb.buffer.type = readOnly ? WGPUBufferBindingType_ReadOnlyStorage
                                          : WGPUBufferBindingType_Storage;
                if (!mergeInto(set, bind, rb))
                    return false;
            }

            // Sampled images (separate_images: SampledImage-type bindings without combined sampler)
            for (const auto &r : res.separate_images)
            {
                uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                const auto &t = compiler.get_type(r.type_id);
                ReflectedBinding rb = buildBase(set, bind);
                rb.hasTexture = true;
                rb.texture.sampleType = SampleTypeFromSpirv(compiler, t);
                rb.texture.viewDimension = DimensionFromSpirv(t);
                rb.texture.multisampled = t.image.ms ? 1u : 0u;
                if (!mergeInto(set, bind, rb))
                    return false;
            }

            // Combined image samplers (HLSL->SPIR-V via DXC often emits these)
            for (const auto &r : res.sampled_images)
            {
                uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                const auto &t = compiler.get_type(r.type_id);
                ReflectedBinding rb = buildBase(set, bind);
                rb.hasTexture = true;
                rb.texture.sampleType = SampleTypeFromSpirv(compiler, t);
                rb.texture.viewDimension = DimensionFromSpirv(t);
                rb.texture.multisampled = t.image.ms ? 1u : 0u;
                if (!mergeInto(set, bind, rb))
                    return false;
            }

            // Separate samplers
            for (const auto &r : res.separate_samplers)
            {
                uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                ReflectedBinding rb = buildBase(set, bind);
                rb.hasSampler = true;
                rb.sampler.type = WGPUSamplerBindingType_Filtering;
                if (!mergeInto(set, bind, rb))
                    return false;
            }

            // Storage images
            for (const auto &r : res.storage_images)
            {
                uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                const auto &t = compiler.get_type(r.type_id);
                spirv_cross::Bitset flags = compiler.get_decoration_bitset(r.id);
                bool readOnly = flags.get(spv::DecorationNonWritable);
                bool writeOnly = flags.get(spv::DecorationNonReadable);
                ReflectedBinding rb = buildBase(set, bind);
                rb.hasStorageTexture = true;
                if (readOnly)
                    rb.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
                else if (writeOnly)
                    rb.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                else
                    rb.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
                rb.storageTexture.format = StorageFormatFromSpirv(t.image.format);
                rb.storageTexture.viewDimension = DimensionFromSpirv(t);
                if (!mergeInto(set, bind, rb))
                    return false;
            }

            return true;
        }
    } // namespace

    static uint64_t ComputeTypeSize(const spirv_cross::Compiler &compiler,
                                    const spirv_cross::SPIRType &type)
    {
        if (!type.array.empty())
        {
            uint64_t elem = ComputeTypeSize(compiler, compiler.get_type(type.parent_type));
            uint64_t count = type.array[0]; // innermost dimension
            for (size_t i = 1; i < type.array.size(); ++i)
                count *= type.array[i];
            return elem * count;
        }
        switch (type.basetype)
        {
        case spirv_cross::SPIRType::Struct:
            return static_cast<uint64_t>(compiler.get_declared_struct_size(type));
        case spirv_cross::SPIRType::Boolean:
            return 4u * type.vecsize * type.columns;
        case spirv_cross::SPIRType::Char:
            return 1u * type.vecsize * type.columns;
        case spirv_cross::SPIRType::SByte:
        case spirv_cross::SPIRType::UByte:
            return 1u * type.vecsize * type.columns;
        case spirv_cross::SPIRType::Short:
        case spirv_cross::SPIRType::UShort:
        case spirv_cross::SPIRType::Half:
            return 2u * type.vecsize * type.columns;
        case spirv_cross::SPIRType::Int:
        case spirv_cross::SPIRType::UInt:
        case spirv_cross::SPIRType::Float:
            return 4u * type.vecsize * type.columns;
        case spirv_cross::SPIRType::Int64:
        case spirv_cross::SPIRType::UInt64:
        case spirv_cross::SPIRType::Double:
            return 8u * type.vecsize * type.columns;
        default:
            return 0;
        }
    }

    bool GetComputeWorkgroupInfo(const std::vector<uint32_t> &spirv,
                                 const std::string &entryPoint,
                                 ComputeWorkgroupInfo &out,
                                 std::string &errMsg)
    {
        if (spirv.empty())
        {
            errMsg = "GetComputeWorkgroupInfo: empty SPIR-V";
            return false;
        }
        spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
        if (!entryPoint.empty())
            compiler.set_entry_point(entryPoint, spv::ExecutionModelGLCompute);

        // Workgroup size from LocalSize execution mode.
        auto execModes = compiler.get_execution_mode_bitset();
        if (execModes.get(spv::ExecutionModeLocalSize))
        {
            out.sizeX = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0);
            out.sizeY = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1);
            out.sizeZ = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2);
            if (out.sizeX == 0)
                out.sizeX = 1;
            if (out.sizeY == 0)
                out.sizeY = 1;
            if (out.sizeZ == 0)
                out.sizeZ = 1;
        }
        else
        {
            out.sizeX = out.sizeY = out.sizeZ = 1;
        }

        // Workgroup storage: walk raw SPIR-V for OpVariable with StorageClass=Workgroup.
        // Format: [wordCount|opcode] [resultTypeId] [resultId] [storageClass] ...
        constexpr uint32_t kOpVariable = 59u;
        constexpr uint32_t kWorkgroup = 4u;
        uint64_t wgBytes = 0;
        for (size_t i = 5; i < spirv.size();)
        {
            uint32_t word0 = spirv[i];
            uint32_t wordCount = word0 >> 16;
            uint32_t opcode = word0 & 0xFFFFu;
            if (wordCount == 0)
                break;
            if (opcode == kOpVariable && wordCount >= 4)
            {
                uint32_t storageClass = spirv[i + 3];
                if (storageClass == kWorkgroup)
                {
                    uint32_t typeId = spirv[i + 1]; // pointer type ID
                    const auto &ptrType = compiler.get_type(typeId);
                    if (ptrType.pointer && ptrType.parent_type != 0)
                    {
                        const auto &elemType = compiler.get_type(ptrType.parent_type);
                        uint64_t sz = ComputeTypeSize(compiler, elemType);
                        sz = (sz + 15ull) & ~15ull;
                        wgBytes += sz;
                    }
                }
            }
            i += wordCount;
        }
        out.workgroupStorageBytes = wgBytes;
        return true;
    }

    std::string ValidateExplicitLayoutCompat(const WGPUPipelineLayoutImpl *layout,
                                             const std::vector<LayoutCompatStageInput> &stages)
    {
        for (const auto &stageIn : stages)
        {
            if (!stageIn.spirv || stageIn.spirv->empty())
                continue;
            try
            {
                spirv_cross::Compiler compiler{stageIn.spirv->data(), stageIn.spirv->size()};
                if (!stageIn.entryPoint.empty())
                    compiler.set_entry_point(stageIn.entryPoint,
                                             static_cast<spv::ExecutionModel>(stageIn.executionModel));
                spirv_cross::ShaderResources res = compiler.get_shader_resources();

                // Look up the BGL entry for (set, binding), check visibility and type.
                auto checkBinding = [&](uint32_t set, uint32_t binding,
                                        auto typeCheck) -> std::string
                {
                    if (set >= layout->bindGroupLayouts.size())
                        return "createRenderPipeline/createComputePipeline: shader uses descriptor set " +
                               std::to_string(set) + " beyond pipeline layout group count";
                    const auto *bgl = layout->bindGroupLayouts[set];
                    if (!bgl)
                        return "createRenderPipeline/createComputePipeline: shader uses descriptor set " +
                               std::to_string(set) + " that is null in the pipeline layout";
                    const WGPUBindGroupLayoutEntryResolved *entry = nullptr;
                    for (const auto &e : bgl->entries)
                    {
                        if (e.binding == binding)
                        {
                            entry = &e;
                            break;
                        }
                    }
                    if (!entry)
                        return "createRenderPipeline/createComputePipeline: shader uses binding " +
                               std::to_string(binding) + " in set " + std::to_string(set) +
                               " not present in the pipeline layout";
                    if (stageIn.stage != WGPUShaderStage_None &&
                        !(entry->visibility & stageIn.stage))
                        return "createRenderPipeline/createComputePipeline: shader binding at set " +
                               std::to_string(set) + ", binding " + std::to_string(binding) +
                               " visibility does not include the shader stage";
                    return typeCheck(*entry);
                };

                auto checkList = [&](const spirv_cross::SmallVector<spirv_cross::Resource> &list,
                                     auto typeCheck) -> std::string
                {
                    for (const auto &r : list)
                    {
                        uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                        uint32_t binding = compiler.get_decoration(r.id, spv::DecorationBinding);
                        if (stageIn.staticallyUsed &&
                            stageIn.staticallyUsed->count(BindingKey{set, binding}) == 0)
                            continue;
                        auto err = checkBinding(set, binding,
                                                [&](const WGPUBindGroupLayoutEntryResolved &e)
                                                { return typeCheck(e, r); });
                        if (!err.empty())
                            return err;
                    }
                    return "";
                };

                // Compatibility rules per WebGPU spec / CTS utils.ts::doResourcesMatch.
                auto matchSampleType = [](WGPUTextureSampleType api, WGPUTextureSampleType wgsl)
                {
                    if (api == WGPUTextureSampleType_Float ||
                        api == WGPUTextureSampleType_UnfilterableFloat)
                        return wgsl == WGPUTextureSampleType_Float ||
                               wgsl == WGPUTextureSampleType_UnfilterableFloat;
                    return api == wgsl;
                };
                auto matchStorageAccess = [](WGPUStorageTextureAccess api, WGPUStorageTextureAccess wgsl)
                {
                    if (api == WGPUStorageTextureAccess_ReadWrite)
                        return wgsl == WGPUStorageTextureAccess_ReadWrite ||
                               wgsl == WGPUStorageTextureAccess_WriteOnly;
                    return api == wgsl;
                };

                // BindingNotUsed (zero-init) and Undefined (user-default) both mean "not this kind".
                auto isUniformBuf = [](const WGPUBindGroupLayoutEntryResolved &e,
                                       const spirv_cross::Resource &) -> std::string
                {
                    if (e.buffer.type != WGPUBufferBindingType_Uniform)
                        return "binding type mismatch: shader expects uniform buffer";
                    return "";
                };
                auto isStorageBuf = [&](const WGPUBindGroupLayoutEntryResolved &e,
                                        const spirv_cross::Resource &r) -> std::string
                {
                    if (e.buffer.type != WGPUBufferBindingType_Storage &&
                        e.buffer.type != WGPUBufferBindingType_ReadOnlyStorage)
                        return "binding type mismatch: shader expects storage buffer";
                    spirv_cross::Bitset flags = compiler.get_buffer_block_flags(r.id);
                    bool shaderReadOnly = flags.get(spv::DecorationNonWritable);
                    bool layoutReadOnly = (e.buffer.type == WGPUBufferBindingType_ReadOnlyStorage);
                    if (shaderReadOnly != layoutReadOnly)
                        return "binding type mismatch: storage buffer access (read-only vs read-write) differs between shader and layout";
                    return "";
                };
                auto isSampler = [&](const WGPUBindGroupLayoutEntryResolved &e,
                                     const spirv_cross::Resource &r) -> std::string
                {
                    if (e.sampler.type == WGPUSamplerBindingType_BindingNotUsed ||
                        e.sampler.type == WGPUSamplerBindingType_Undefined)
                        return "binding type mismatch: shader expects sampler, layout has non-sampler";
                    if (stageIn.comparisonSamplers)
                    {
                        uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                        uint32_t binding = compiler.get_decoration(r.id, spv::DecorationBinding);
                        bool shaderCmp = stageIn.comparisonSamplers->count(BindingKey{set, binding}) != 0;
                        bool layoutCmp = (e.sampler.type == WGPUSamplerBindingType_Comparison);
                        if (shaderCmp != layoutCmp)
                            return "binding type mismatch: sampler comparison-ness differs between shader and layout";
                    }
                    return "";
                };
                auto isSampledTex = [&](const WGPUBindGroupLayoutEntryResolved &e,
                                        const spirv_cross::Resource &r) -> std::string
                {
                    if (e.texture.sampleType == WGPUTextureSampleType_BindingNotUsed ||
                        e.texture.sampleType == WGPUTextureSampleType_Undefined)
                        return "binding type mismatch: shader expects sampled texture, layout has non-texture";
                    const auto &t = compiler.get_type(r.type_id);
                    WGPUTextureSampleType wgslSampleType = SampleTypeFromSpirv(compiler, t);
                    WGPUTextureViewDimension wgslDim = DimensionFromSpirv(t);
                    uint32_t wgslMs = t.image.ms ? 1u : 0u;
                    if (!matchSampleType(e.texture.sampleType, wgslSampleType))
                        return "binding type mismatch: sampled texture sampleType differs between shader and layout";
                    if (e.texture.viewDimension != wgslDim)
                        return "binding type mismatch: sampled texture viewDimension differs between shader and layout";
                    if (e.texture.multisampled != wgslMs)
                        return "binding type mismatch: sampled texture multisampled differs between shader and layout";
                    return "";
                };
                auto isStorageTex = [&](const WGPUBindGroupLayoutEntryResolved &e,
                                        const spirv_cross::Resource &r) -> std::string
                {
                    if (e.storageTexture.access == WGPUStorageTextureAccess_BindingNotUsed ||
                        e.storageTexture.access == WGPUStorageTextureAccess_Undefined)
                        return "binding type mismatch: shader expects storage texture, layout has non-storage-texture";
                    const auto &t = compiler.get_type(r.type_id);
                    spirv_cross::Bitset flags = compiler.get_decoration_bitset(r.id);
                    bool readOnly = flags.get(spv::DecorationNonWritable);
                    bool writeOnly = flags.get(spv::DecorationNonReadable);
                    WGPUStorageTextureAccess wgslAccess =
                        readOnly    ? WGPUStorageTextureAccess_ReadOnly
                        : writeOnly ? WGPUStorageTextureAccess_WriteOnly
                                    : WGPUStorageTextureAccess_ReadWrite;
                    WGPUTextureFormat wgslFormat = StorageFormatFromSpirv(t.image.format);
                    WGPUTextureViewDimension wgslDim = DimensionFromSpirv(t);
                    if (!matchStorageAccess(e.storageTexture.access, wgslAccess))
                        return "binding type mismatch: storage texture access differs between shader and layout";
                    if (e.storageTexture.format != wgslFormat)
                        return "binding type mismatch: storage texture format differs between shader and layout";
                    if (e.storageTexture.viewDimension != wgslDim)
                        return "binding type mismatch: storage texture viewDimension differs between shader and layout";
                    return "";
                };

                std::string err;
                if (!(err = checkList(res.uniform_buffers, isUniformBuf)).empty())
                    return err;
                if (!(err = checkList(res.storage_buffers, isStorageBuf)).empty())
                    return err;
                if (!(err = checkList(res.separate_samplers, isSampler)).empty())
                    return err;
                if (!(err = checkList(res.separate_images, isSampledTex)).empty())
                    return err;
                if (!(err = checkList(res.sampled_images, isSampledTex)).empty())
                    return err;
                if (!(err = checkList(res.storage_images, isStorageTex)).empty())
                    return err;
            }
            catch (const std::exception &e)
            {
                return std::string("layout-compat validation: SPIR-V reflection threw: ") + e.what();
            }
            catch (...)
            {
                return "layout-compat validation: SPIR-V reflection threw an unknown exception";
            }
        }
        return "";
    }

    std::string ValidateTextureSamplerPairs(const std::vector<uint32_t> &spirv, const WGPUPipelineLayoutImpl *layout)
    {
        if (!layout || spirv.size() < 5)
            return {};

        struct VarBind
        {
            uint32_t set = UINT32_MAX;
            uint32_t binding = UINT32_MAX;
        };
        std::map<uint32_t, VarBind> varBinds;
        // Pointer-producing chains: OpAccessChain result -> base variable id.
        std::map<uint32_t, uint32_t> chainBase;
        // OpLoad result -> ultimate variable id (through chains).
        std::map<uint32_t, uint32_t> loadedVar;

        struct Pair
        {
            uint32_t imageVar;
            uint32_t samplerVar;
        };
        std::vector<Pair> pairs;

        const uint32_t *code = spirv.data();
        const size_t numWords = spirv.size();
        size_t w = 5;
        while (w < numWords)
        {
            uint32_t first = code[w];
            uint32_t wc = first >> 16;
            uint32_t op = first & 0xFFFF;
            if (wc == 0 || w + wc > numWords)
                break;

            switch (op)
            {
            case spv::OpDecorate:
                if (wc >= 4)
                {
                    uint32_t target = code[w + 1];
                    uint32_t dec = code[w + 2];
                    if (dec == spv::DecorationDescriptorSet)
                        varBinds[target].set = code[w + 3];
                    else if (dec == spv::DecorationBinding)
                        varBinds[target].binding = code[w + 3];
                }
                break;
            case spv::OpAccessChain:
            case spv::OpInBoundsAccessChain:
                if (wc >= 4)
                {
                    uint32_t resultId = code[w + 2];
                    uint32_t baseId = code[w + 3];
                    auto it = chainBase.find(baseId);
                    chainBase[resultId] = (it != chainBase.end()) ? it->second : baseId;
                }
                break;
            case spv::OpLoad:
                if (wc >= 4)
                {
                    uint32_t resultId = code[w + 2];
                    uint32_t ptrId = code[w + 3];
                    auto it = chainBase.find(ptrId);
                    loadedVar[resultId] = (it != chainBase.end()) ? it->second : ptrId;
                }
                break;
            case spv::OpSampledImage:
                if (wc >= 5)
                {
                    uint32_t imgId = code[w + 3];
                    uint32_t smpId = code[w + 4];
                    auto ii = loadedVar.find(imgId);
                    auto si = loadedVar.find(smpId);
                    if (ii != loadedVar.end() && si != loadedVar.end())
                        pairs.push_back({ii->second, si->second});
                }
                break;
            default:
                break;
            }
            w += wc;
        }

        if (pairs.empty())
            return {};

        auto findEntry = [&](uint32_t set, uint32_t binding) -> const WGPUBindGroupLayoutEntryResolved *
        {
            if (set >= layout->bindGroupLayouts.size())
                return nullptr;
            const WGPUBindGroupLayoutImpl *bgl = layout->bindGroupLayouts[set];
            if (!bgl)
                return nullptr;
            for (const auto &e : bgl->entries)
                if (e.binding == binding)
                    return &e;
            return nullptr;
        };

        for (const Pair &p : pairs)
        {
            auto iv = varBinds.find(p.imageVar);
            auto sv = varBinds.find(p.samplerVar);
            if (iv == varBinds.end() || sv == varBinds.end())
                continue;
            if (iv->second.set == UINT32_MAX || sv->second.set == UINT32_MAX)
                continue;

            const auto *texEntry = findEntry(iv->second.set, iv->second.binding);
            const auto *smpEntry = findEntry(sv->second.set, sv->second.binding);
            if (!texEntry || !smpEntry)
                continue;
            if (smpEntry->sampler.type != WGPUSamplerBindingType_Filtering)
                continue;
            if (texEntry->texture.sampleType == WGPUTextureSampleType_BindingNotUsed ||
                texEntry->texture.sampleType == WGPUTextureSampleType_Undefined ||
                texEntry->texture.sampleType == WGPUTextureSampleType_Float)
                continue;

            return "texture at group " + std::to_string(iv->second.set) +
                   ", binding " + std::to_string(iv->second.binding) +
                   " has non-float sampleType and is used with filtering sampler at group " +
                   std::to_string(sv->second.set) + ", binding " +
                   std::to_string(sv->second.binding);
        }
        return {};
    }

    bool GetUsedDescriptorSets(const std::vector<uint32_t> &spirv,
                               const std::string &entryPoint,
                               uint32_t executionModel,
                               std::set<uint32_t> &outSets,
                               std::string &errMsg)
    {
        if (spirv.empty())
        {
            errMsg = "GetUsedDescriptorSets: empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
            if (!entryPoint.empty())
                compiler.set_entry_point(entryPoint, static_cast<spv::ExecutionModel>(executionModel));
            auto activeVars = compiler.get_active_interface_variables();
            spirv_cross::ShaderResources res = compiler.get_shader_resources(activeVars);
            auto collect = [&](const spirv_cross::SmallVector<spirv_cross::Resource> &list)
            {
                for (const auto &r : list)
                    outSets.insert(compiler.get_decoration(r.id, spv::DecorationDescriptorSet));
            };
            collect(res.uniform_buffers);
            collect(res.storage_buffers);
            collect(res.sampled_images);
            collect(res.separate_images);
            collect(res.separate_samplers);
            collect(res.storage_images);
        }
        catch (...)
        {
            errMsg = "GetUsedDescriptorSets: spirv_cross exception";
            return false;
        }
        return true;
    }

    WGPUPipelineLayoutImpl *BuildAutoPipelineLayout(WGPUDeviceImpl *device,
                                                    const std::vector<AutoLayoutStageInput> &stages,
                                                    std::string &errMsg)
    {
        std::map<uint32_t, std::map<uint32_t, ReflectedBinding>> merged;
        for (const auto &s : stages)
        {
            if (!ReflectOneStage(s, merged, errMsg))
                return nullptr;
        }

        const uint32_t numSets = merged.empty() ? 0u : (merged.rbegin()->first + 1u);

        auto *pl = new WGPUPipelineLayoutImpl();
        pl->device = device;
        device->refCount.fetch_add(1, std::memory_order_relaxed);
        pl->bindGroupLayouts.resize(numSets, nullptr);

        auto vkDev = device->rhi->GetDevice();
        std::vector<vk::DescriptorSetLayout> vkSetLayouts(numSets, VK_NULL_HANDLE);

        for (uint32_t s = 0; s < numSets; ++s)
        {
            auto *bgl = new WGPUBindGroupLayoutImpl();
            bgl->device = device;
            device->refCount.fetch_add(1, std::memory_order_relaxed);
            bgl->exclusivePipeline = pl;
            pl->bindGroupLayouts[s] = bgl;

            auto it = merged.find(s);
            if (it == merged.end() || it->second.empty())
            {
                vk::DescriptorSetLayoutCreateInfo emptyCI{};
                auto emptyLayout = vkDev.createDescriptorSetLayout(emptyCI);
                pl->ownedEmptySetLayouts.push_back(emptyLayout);
                vkSetLayouts[s] = emptyLayout;
                continue;
            }

            std::vector<pe::DescriptorBindingInfo> infos;
            vk::ShaderStageFlags stageMask{};
            bgl->entries.reserve(it->second.size());
            for (const auto &kv : it->second)
            {
                const ReflectedBinding &rb = kv.second;
                WGPUBindGroupLayoutEntryResolved e{};
                e.binding = rb.binding;
                e.visibility = rb.visibility;
                e.buffer = rb.buffer;
                e.sampler = rb.sampler;
                e.texture = rb.texture;
                e.storageTexture = rb.storageTexture;
                bgl->entries.push_back(e);

                pe::DescriptorBindingInfo info{};
                info.binding = rb.binding;
                info.count = 1;
                if (rb.hasBuffer)
                {
                    info.type = (rb.buffer.type == WGPUBufferBindingType_Uniform)
                                    ? vk::DescriptorType::eUniformBuffer
                                    : vk::DescriptorType::eStorageBuffer;
                }
                else if (rb.hasSampler)
                {
                    info.type = vk::DescriptorType::eSampler;
                }
                else if (rb.hasTexture)
                {
                    info.type = vk::DescriptorType::eSampledImage;
                    info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                }
                else if (rb.hasStorageTexture)
                {
                    info.type = vk::DescriptorType::eStorageImage;
                    info.imageLayout = vk::ImageLayout::eGeneral;
                }
                vk::ShaderStageFlags entryStages{};
                if (rb.visibility & WGPUShaderStage_Vertex)
                    entryStages |= vk::ShaderStageFlagBits::eVertex;
                if (rb.visibility & WGPUShaderStage_Fragment)
                    entryStages |= vk::ShaderStageFlagBits::eFragment;
                if (rb.visibility & WGPUShaderStage_Compute)
                    entryStages |= vk::ShaderStageFlagBits::eCompute;
                stageMask |= entryStages;
                infos.push_back(info);
            }
            bgl->bindingInfos = infos;
            bgl->stage = stageMask;
            if (!infos.empty())
            {
                bgl->layout = pe::DescriptorLayout::Create(infos, stageMask, "auto_bgl");
                vkSetLayouts[s] = bgl->layout->ApiHandle();
            }
            else
            {
                vk::DescriptorSetLayoutCreateInfo emptyCI{};
                auto emptyLayout = vkDev.createDescriptorSetLayout(emptyCI);
                pl->ownedEmptySetLayouts.push_back(emptyLayout);
                vkSetLayouts[s] = emptyLayout;
            }
        }

        vk::PipelineLayoutCreateInfo ci{};
        ci.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
        ci.pSetLayouts = vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
        pl->vkLayout = vkDev.createPipelineLayout(ci);
        return pl;
    }

    bool GetFragmentOutputTypes(const std::vector<uint32_t> &spirv,
                                const std::string &entryPoint,
                                std::map<uint32_t, FragmentOutputInfo> &outInfos,
                                std::string &errMsg)
    {
        if (spirv.empty())
        {
            errMsg = "empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
            if (!entryPoint.empty())
                compiler.set_entry_point(entryPoint, spv::ExecutionModelFragment);
            const auto &outputs = compiler.get_shader_resources().stage_outputs;
            for (const auto &r : outputs)
            {
                uint32_t loc = compiler.get_decoration(r.id, spv::DecorationLocation);
                const spirv_cross::SPIRType &t = compiler.get_type(r.type_id);
                FragmentOutputInfo info{};
                info.vecSize = t.vecsize;
                switch (t.basetype)
                {
                case spirv_cross::SPIRType::Float:
                case spirv_cross::SPIRType::Half:
                case spirv_cross::SPIRType::Double:
                    info.baseType = ShaderOutputBaseType::Float;
                    break;
                case spirv_cross::SPIRType::UInt:
                case spirv_cross::SPIRType::UInt64:
                    info.baseType = ShaderOutputBaseType::Uint;
                    break;
                case spirv_cross::SPIRType::Int:
                case spirv_cross::SPIRType::Int64:
                    info.baseType = ShaderOutputBaseType::Sint;
                    break;
                default:
                    break;
                }
                outInfos[loc] = info;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            errMsg = e.what();
            return false;
        }
        catch (...)
        {
            errMsg = "unknown error";
            return false;
        }
    }

    bool GetVertexInputTypes(const std::vector<uint32_t> &spirv,
                             const std::string &entryPoint,
                             std::map<uint32_t, VertexInputInfo> &outInfos,
                             std::string &errMsg)
    {
        if (spirv.empty())
        {
            errMsg = "empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
            if (!entryPoint.empty())
                compiler.set_entry_point(entryPoint, spv::ExecutionModelVertex);
            const auto &inputs = compiler.get_shader_resources().stage_inputs;
            for (const auto &r : inputs)
            {
                uint32_t loc = compiler.get_decoration(r.id, spv::DecorationLocation);
                const spirv_cross::SPIRType &t = compiler.get_type(r.type_id);
                VertexInputInfo info{};
                info.vecSize = t.vecsize;
                switch (t.basetype)
                {
                case spirv_cross::SPIRType::Float:
                case spirv_cross::SPIRType::Half:
                case spirv_cross::SPIRType::Double:
                    info.baseType = ShaderOutputBaseType::Float;
                    break;
                case spirv_cross::SPIRType::UInt:
                case spirv_cross::SPIRType::UInt64:
                    info.baseType = ShaderOutputBaseType::Uint;
                    break;
                case spirv_cross::SPIRType::Int:
                case spirv_cross::SPIRType::Int64:
                    info.baseType = ShaderOutputBaseType::Sint;
                    break;
                default:
                    break;
                }
                outInfos[loc] = info;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            errMsg = e.what();
            return false;
        }
        catch (...)
        {
            errMsg = "unknown error";
            return false;
        }
    }

    bool GetVaryingInfos(const std::vector<uint32_t> &spirv,
                         const std::string &entryPoint,
                         uint32_t executionModel,
                         bool getInputs,
                         std::map<uint32_t, VaryingInfo> &outInfos,
                         std::string &errMsg)
    {
        if (spirv.empty())
        {
            errMsg = "empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
            if (!entryPoint.empty())
                compiler.set_entry_point(entryPoint, static_cast<spv::ExecutionModel>(executionModel));
            const auto &res = compiler.get_shader_resources();
            const auto &vars = getInputs ? res.stage_inputs : res.stage_outputs;
            for (const auto &r : vars)
            {
                if (!compiler.has_decoration(r.id, spv::DecorationLocation))
                    continue;
                uint32_t loc = compiler.get_decoration(r.id, spv::DecorationLocation);
                const spirv_cross::SPIRType &t = compiler.get_type(r.type_id);
                VaryingInfo info{};
                info.vecSize = t.vecsize;
                switch (t.basetype)
                {
                case spirv_cross::SPIRType::Float:
                case spirv_cross::SPIRType::Half:
                case spirv_cross::SPIRType::Double:
                    info.baseType = ShaderOutputBaseType::Float;
                    break;
                case spirv_cross::SPIRType::UInt:
                case spirv_cross::SPIRType::UInt64:
                    info.baseType = ShaderOutputBaseType::Uint;
                    break;
                case spirv_cross::SPIRType::Int:
                case spirv_cross::SPIRType::Int64:
                    info.baseType = ShaderOutputBaseType::Sint;
                    break;
                default:
                    break;
                }
                info.isFlat = compiler.has_decoration(r.id, spv::DecorationFlat);
                info.isNoPerspective = compiler.has_decoration(r.id, spv::DecorationNoPerspective);
                info.isCentroid = compiler.has_decoration(r.id, spv::DecorationCentroid);
                info.isSample = compiler.has_decoration(r.id, spv::DecorationSample);
                outInfos[loc] = info;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            errMsg = e.what();
            return false;
        }
        catch (...)
        {
            errMsg = "unknown error";
            return false;
        }
    }

    bool CountFragmentInterStageBuiltins(const std::vector<uint32_t> &spirv,
                                         const std::string &entryPoint,
                                         uint32_t &outCount,
                                         std::string &errMsg)
    {
        outCount = 0;
        if (spirv.empty())
        {
            errMsg = "empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
            if (!entryPoint.empty())
                compiler.set_entry_point(entryPoint, spv::ExecutionModelFragment);
            const auto &res = compiler.get_shader_resources();
            // Inter-Stage Builtins per the WebGPU spec (§10.3.1)
            static const std::set<spv::BuiltIn> kInterStageBuiltins = {
                spv::BuiltInFrontFacing,
                spv::BuiltInSampleId,
                spv::BuiltInSampleMask,
                spv::BuiltInPrimitiveId,
                spv::BuiltInSubgroupLocalInvocationId,
                spv::BuiltInSubgroupSize,
            };
            // spirv_cross separates builtins into builtin_inputs
            for (const auto &bir : res.builtin_inputs)
            {
                if (kInterStageBuiltins.count(bir.builtin))
                    ++outCount;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            errMsg = e.what();
            return false;
        }
        catch (...)
        {
            errMsg = "unknown error";
            return false;
        }
    }

    bool GetFragmentOutputBuiltins(const std::vector<uint32_t> &spirv,
                                   const std::string &entryPoint,
                                   FragmentOutputBuiltins &out,
                                   std::string &errMsg)
    {
        out = {};
        if (spirv.empty())
        {
            errMsg = "empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
            if (!entryPoint.empty())
                compiler.set_entry_point(entryPoint, spv::ExecutionModelFragment);
            const auto &res = compiler.get_shader_resources();
            for (const auto &bor : res.builtin_outputs)
            {
                if (bor.builtin == spv::BuiltInFragDepth)
                    out.hasFragDepth = true;
                else if (bor.builtin == spv::BuiltInSampleMask)
                    out.hasSampleMask = true;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            errMsg = e.what();
            return false;
        }
        catch (...)
        {
            errMsg = "unknown error";
            return false;
        }
    }

    uint32_t CountVertexClipDistancesArraySize(const std::vector<uint32_t> &spirv,
                                               const std::string &entryPoint,
                                               std::string &errMsg)
    {
        errMsg.clear();
        if (spirv.size() < 5)
            return 0;

        const uint32_t *code = spirv.data();
        const size_t numWords = spirv.size();

        // First pass: find OpEntryPoint matching name + ExecutionModelVertex
        // and collect its interface variable ids. Also gather OpDecorate /
        // OpMemberDecorate BuiltIn ClipDistance, OpVariable (id -> pointer
        // type id, storage class), OpTypePointer (ptr -> pointee), OpTypeArray
        // (arr -> {element, lengthId}), OpTypeStruct (members), and OpConstant
        // (id -> value).

        std::set<uint32_t> entryInterface;
        bool foundEntry = false;
        std::set<uint32_t> clipDistVarIds;           // direct OpDecorate targets
        std::map<uint32_t, uint32_t> clipDistMember; // struct typeId -> member idx
        std::map<uint32_t, uint32_t> varPtrType;     // varId -> pointer typeId
        std::map<uint32_t, uint32_t> varStorage;     // varId -> storage class
        std::map<uint32_t, uint32_t> ptrPointee;     // ptr typeId -> pointee typeId
        struct ArrInfo
        {
            uint32_t elemTypeId;
            uint32_t lengthId;
        };
        std::map<uint32_t, ArrInfo> arrInfo;                     // array typeId -> {elem, len}
        std::map<uint32_t, std::vector<uint32_t>> structMembers; // structId -> member type ids
        std::map<uint32_t, uint32_t> constU32;                   // const id -> uint value

        constexpr uint32_t kExecModelVertex = 0u;

        for (size_t w = 5; w < numWords;)
        {
            uint32_t word0 = code[w];
            uint32_t wc = word0 >> 16;
            uint32_t op = word0 & 0xFFFFu;
            if (wc == 0 || w + wc > numWords)
                break;

            switch (op)
            {
            case spv::OpEntryPoint:
                // [op|wc] [execModel] [entryId] [name literal...] [iface ids...]
                if (wc >= 4)
                {
                    uint32_t execModel = code[w + 1];
                    // Parse inline null-terminated name starting at word w+3.
                    size_t nameStart = w + 3;
                    size_t k = nameStart;
                    std::string name;
                    bool nameDone = false;
                    while (k < w + wc && !nameDone)
                    {
                        uint32_t word = code[k];
                        for (int b = 0; b < 4; ++b)
                        {
                            char c = static_cast<char>((word >> (b * 8)) & 0xFFu);
                            if (c == '\0')
                            {
                                nameDone = true;
                                break;
                            }
                            name.push_back(c);
                        }
                        ++k;
                    }
                    // Interface ids start after the name literal words.
                    if (execModel == kExecModelVertex &&
                        (entryPoint.empty() || name == entryPoint))
                    {
                        foundEntry = true;
                        for (size_t j = k; j < w + wc; ++j)
                            entryInterface.insert(code[j]);
                    }
                }
                break;

            case spv::OpDecorate:
                // [op|wc] [target] [decoration] [operands...]
                if (wc >= 4)
                {
                    uint32_t target = code[w + 1];
                    uint32_t dec = code[w + 2];
                    if (dec == spv::DecorationBuiltIn &&
                        code[w + 3] == spv::BuiltInClipDistance)
                    {
                        clipDistVarIds.insert(target);
                    }
                }
                break;

            case spv::OpMemberDecorate:
                // [op|wc] [structType] [memberIdx] [decoration] [operands...]
                if (wc >= 5)
                {
                    uint32_t structId = code[w + 1];
                    uint32_t memberIdx = code[w + 2];
                    uint32_t dec = code[w + 3];
                    if (dec == spv::DecorationBuiltIn &&
                        code[w + 4] == spv::BuiltInClipDistance)
                    {
                        clipDistMember[structId] = memberIdx;
                    }
                }
                break;

            case spv::OpVariable:
                // [op|wc] [resultType] [resultId] [storageClass] [initializer?]
                if (wc >= 4)
                {
                    uint32_t resultType = code[w + 1];
                    uint32_t resultId = code[w + 2];
                    uint32_t storage = code[w + 3];
                    varPtrType[resultId] = resultType;
                    varStorage[resultId] = storage;
                }
                break;

            case spv::OpTypePointer:
                // [op|wc] [resultId] [storageClass] [pointeeType]
                if (wc >= 4)
                {
                    uint32_t resultId = code[w + 1];
                    uint32_t pointee = code[w + 3];
                    ptrPointee[resultId] = pointee;
                }
                break;

            case spv::OpTypeArray:
                // [op|wc] [resultId] [elementType] [lengthId]
                if (wc >= 4)
                {
                    uint32_t resultId = code[w + 1];
                    arrInfo[resultId] = {code[w + 2], code[w + 3]};
                }
                break;

            case spv::OpTypeStruct:
                // [op|wc] [resultId] [memberTypes...]
                if (wc >= 2)
                {
                    uint32_t resultId = code[w + 1];
                    std::vector<uint32_t> members;
                    members.reserve(wc - 2);
                    for (uint32_t j = 2; j < wc; ++j)
                        members.push_back(code[w + j]);
                    structMembers[resultId] = std::move(members);
                }
                break;

            case spv::OpConstant:
                // [op|wc] [resultType] [resultId] [valueWords...]
                // We only care about 32-bit unsigned/signed literal values.
                if (wc >= 4)
                {
                    uint32_t resultId = code[w + 2];
                    constU32[resultId] = code[w + 3];
                }
                break;

            default:
                break;
            }

            w += wc;
        }

        if (!foundEntry)
            return 0;

        // Helper: given an interface variable id, if its pointee (or a struct
        // member in its pointee) is the clip-distances array, return the
        // array length; else 0.
        auto resolveArrayLength = [&](uint32_t arrayTypeId) -> uint32_t
        {
            auto it = arrInfo.find(arrayTypeId);
            if (it == arrInfo.end())
                return 0;
            auto cit = constU32.find(it->second.lengthId);
            if (cit == constU32.end())
                return 0;
            return cit->second;
        };

        for (uint32_t varId : entryInterface)
        {
            auto ptrIt = varPtrType.find(varId);
            if (ptrIt == varPtrType.end())
                continue;
            auto pointeeIt = ptrPointee.find(ptrIt->second);
            if (pointeeIt == ptrPointee.end())
                continue;
            uint32_t pointee = pointeeIt->second;

            // Case 1: direct OpDecorate BuiltIn ClipDistance on this variable.
            // The pointee is OpTypeArray itself.
            if (clipDistVarIds.count(varId))
            {
                uint32_t n = resolveArrayLength(pointee);
                if (n != 0)
                    return n;
            }

            // Case 2: variable is a block (e.g. gl_PerVertex) and one of its
            // members is OpMemberDecorate BuiltIn ClipDistance. Pointee is a
            // struct; the decorated member's type is the array.
            auto cmIt = clipDistMember.find(pointee);
            if (cmIt != clipDistMember.end())
            {
                auto smIt = structMembers.find(pointee);
                if (smIt != structMembers.end() &&
                    cmIt->second < smIt->second.size())
                {
                    uint32_t arrTypeId = smIt->second[cmIt->second];
                    uint32_t n = resolveArrayLength(arrTypeId);
                    if (n != 0)
                        return n;
                }
            }
        }

        return 0;
    }

    bool HasBlendSrc1Output(const std::vector<uint32_t> &spirv,
                            const std::string &entryPoint,
                            std::string &errMsg)
    {
        if (spirv.empty())
        {
            errMsg = "empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler compiler{spirv.data(), spirv.size()};
            if (!entryPoint.empty())
                compiler.set_entry_point(entryPoint, spv::ExecutionModelFragment);
            const auto &outputs = compiler.get_shader_resources().stage_outputs;
            for (const auto &r : outputs)
            {
                // WGSL @blend_src(1) lowers to SPIR-V variable with both
                // Location and Index decorations. Index 1 is the dual-source
                // second color input.
                if (!compiler.has_decoration(r.id, spv::DecorationIndex))
                    continue;
                if (compiler.get_decoration(r.id, spv::DecorationIndex) == 1u)
                    return true;
            }
            return false;
        }
        catch (const std::exception &e)
        {
            errMsg = e.what();
            return false;
        }
        catch (...)
        {
            errMsg = "unknown error";
            return false;
        }
    }
} // namespace pwgpu
