#include "Reflect.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "BindGroup.h"
#include "FormatMap.h"
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

        // W3C §10.3.6 default pipeline layout: f32 sample type defaults to
        // "unfilterable-float" unless the resource is statically used in a texture
        // builtin call that also uses a sampler (paired via OpSampledImage).
        WGPUTextureSampleType SampleTypeFromSpirv(const spirv_cross::Compiler &c,
                                                  const spirv_cross::SPIRType &t,
                                                  bool samplerPaired = true)
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
                return samplerPaired ? WGPUTextureSampleType_Float
                                     : WGPUTextureSampleType_UnfilterableFloat;
            }
        }

        // OpVariable IDs of images bound as the image operand of OpSampledImage,
        // restricted to functions transitively reachable from the named
        // entry point. Per W3C §10.3.6 the auto-layout sampleType is computed
        // *per entry point*: only static uses by THIS entry point promote
        // f32 textures from "unfilterable-float" to "float". A whole-module
        // walk would leak sampler-pair facts from sibling entry points and
        // wrongly classify load-only bindings as "float" in a stage that
        // never samples them. Walk pattern mirrors ValidateTextureSamplerPairs.
        std::set<uint32_t> CollectSampledImageVarIds(const std::vector<uint32_t> &spirv,
                                                     const std::string &entryName,
                                                     uint32_t executionModel)
        {
            std::set<uint32_t> result;
            if (spirv.size() < 5)
                return result;

            const uint32_t *code = spirv.data();
            const size_t numWords = spirv.size();

            // Pass 1: locate OpEntryPoint matching (name, execModel) and
            // record its function id.
            uint32_t entryFnId = 0;
            bool foundEntry = false;
            for (size_t w = 5; w < numWords;)
            {
                uint32_t first = code[w];
                uint32_t wc = first >> 16;
                uint32_t op = first & 0xFFFF;
                if (wc == 0 || w + wc > numWords)
                    break;
                if (op == spv::OpEntryPoint && wc >= 4)
                {
                    uint32_t mode = code[w + 1];
                    uint32_t fnId = code[w + 2];
                    // Decode inline name literal (4 chars/word, NUL-terminated).
                    std::string name;
                    for (size_t k = w + 3; k < w + wc; ++k)
                    {
                        uint32_t word = code[k];
                        bool done = false;
                        for (int b = 0; b < 4; ++b)
                        {
                            char c = static_cast<char>((word >> (b * 8)) & 0xFFu);
                            if (c == '\0')
                            {
                                done = true;
                                break;
                            }
                            name.push_back(c);
                        }
                        if (done)
                            break;
                    }
                    if (mode == executionModel && (entryName.empty() || name == entryName))
                    {
                        entryFnId = fnId;
                        foundEntry = true;
                        break;
                    }
                }
                w += wc;
            }

            // Pass 2: build call graph (caller fn id → set of callee fn ids)
            // by tracking the current function via OpFunction/OpFunctionEnd and
            // recording each OpFunctionCall target. Skipped when entry not
            // found — fallback below treats every function as reachable.
            std::set<uint32_t> reachable;
            if (foundEntry)
            {
                std::map<uint32_t, std::set<uint32_t>> callGraph;
                uint32_t currentFn = 0;
                for (size_t w = 5; w < numWords;)
                {
                    uint32_t first = code[w];
                    uint32_t wc = first >> 16;
                    uint32_t op = first & 0xFFFF;
                    if (wc == 0 || w + wc > numWords)
                        break;
                    if (op == spv::OpFunction && wc >= 5)
                        currentFn = code[w + 2];
                    else if (op == spv::OpFunctionEnd)
                        currentFn = 0;
                    else if (op == spv::OpFunctionCall && wc >= 4 && currentFn != 0)
                        callGraph[currentFn].insert(code[w + 3]);
                    w += wc;
                }
                // Pass 3: BFS from entry function id.
                std::vector<uint32_t> stack{entryFnId};
                while (!stack.empty())
                {
                    uint32_t fn = stack.back();
                    stack.pop_back();
                    if (!reachable.insert(fn).second)
                        continue;
                    auto it = callGraph.find(fn);
                    if (it != callGraph.end())
                    {
                        for (uint32_t callee : it->second)
                            stack.push_back(callee);
                    }
                }
            }

            // Pass 4: walk for OpSampledImage, but only inside reachable
            // functions. When reachability lookup failed (no matching entry
            // point in the module), fall back to whole-module walk to
            // preserve correctness on direct-SPIR-V test paths and on
            // shaders without a matching name.
            std::map<uint32_t, uint32_t> chainBase;
            std::map<uint32_t, uint32_t> loadedVar;
            uint32_t currentFn = 0;
            for (size_t w = 5; w < numWords;)
            {
                uint32_t first = code[w];
                uint32_t wc = first >> 16;
                uint32_t op = first & 0xFFFF;
                if (wc == 0 || w + wc > numWords)
                    break;

                if (op == spv::OpFunction && wc >= 5)
                {
                    currentFn = code[w + 2];
                }
                else if (op == spv::OpFunctionEnd)
                {
                    currentFn = 0;
                }
                else
                {
                    const bool inReach = !foundEntry ||
                                         (currentFn != 0 && reachable.count(currentFn) > 0);
                    if (inReach)
                    {
                        switch (op)
                        {
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
                                auto ii = loadedVar.find(imgId);
                                if (ii != loadedVar.end())
                                    result.insert(ii->second);
                            }
                            break;
                        default:
                            break;
                        }
                    }
                }
                w += wc;
            }
            return result;
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

        template <typename StageInput>
        WGPUTextureFormat StorageFormatForBinding(const StageInput &stage,
                                                  uint32_t set,
                                                  uint32_t binding,
                                                  WGPUTextureFormat spirvFormat)
        {
            if (stage.storageTextureFormats)
            {
                auto it = stage.storageTextureFormats->find(BindingKey{set, binding});
                if (it != stage.storageTextureFormats->end() &&
                    it->second != WGPUTextureFormat_Undefined)
                    return it->second;
            }
            return spirvFormat;
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
            // spirv-cross set_entry_point throws when the named entry point
            // isn't in the SPIR-V (placeholder SPIR-V with mismatched JS
            // reflection — texture_external, naga-panic fallback). Catch
            // here; otherwise the exception unwinds through the extern "C"
            // boundary in wgpuDeviceCreateRenderPipeline and aborts.
            std::unique_ptr<spirv_cross::Compiler> compilerPtr;
            try
            {
                compilerPtr = std::make_unique<spirv_cross::Compiler>(stage.spirv->data(),
                                                                      stage.spirv->size());
                if (!stage.entryPoint.empty())
                    compilerPtr->set_entry_point(stage.entryPoint,
                                                 static_cast<spv::ExecutionModel>(stage.executionModel));
            }
            catch (const std::exception &e)
            {
                errMsg = std::string("auto-layout: spirv-cross rejected entry point '") +
                         stage.entryPoint + "': " + e.what();
                return false;
            }
            spirv_cross::Compiler &compiler = *compilerPtr;
            // get_decoration / get_type / get_buffer_block_flags can also throw
            // on malformed SPIR-V; catch here so the unwind doesn't cross extern "C".
            try
            {
                // Per W3C §10.3.6 the auto-layout BGL contains exactly the
                // resources *statically used by entryPoint*. Prefer the JS
                // frontend's per-entry-point reflection (naga side-channel) when
                // it provides usable data — naga compiles to SPIR-V 1.0/1.3
                // where OpEntryPoint omits descriptor-bound globals, so
                // spirv-cross's get_active_interface_variables() can miss
                // fragment-stage storage buffers. Fall back to the active-
                // interface walk when reflection isn't present or didn't supply
                // any pairs for this entry point — that matches pre-2026-04-26
                // behavior and avoids over-filtering shaders for which we have
                // no authoritative use-set.
                const bool useStaticallyUsed =
                    stage.staticallyUsedAuthoritative ||
                    (stage.staticallyUsed && !stage.staticallyUsed->empty()) ||
                    (stage.staticallyUsedNames && !stage.staticallyUsedNames->empty()) ||
                    (stage.staticallyUsedResources && !stage.staticallyUsedResources->empty());
                spirv_cross::ShaderResources res =
                    useStaticallyUsed
                        ? compiler.get_shader_resources()
                        : compiler.get_shader_resources(
                              compiler.get_active_interface_variables());
                const std::set<uint32_t> sampledImageVarIds =
                    CollectSampledImageVarIds(*stage.spirv, stage.entryPoint, stage.executionModel);

                auto isStaticallyUsed = [&](uint32_t resourceId, const char *resourceKind) -> bool
                {
                    if (!useStaticallyUsed)
                        return true;
                    uint32_t set = compiler.get_decoration(resourceId, spv::DecorationDescriptorSet);
                    uint32_t bind = compiler.get_decoration(resourceId, spv::DecorationBinding);
                    if (stage.staticallyUsedResources && !stage.staticallyUsedResources->empty())
                    {
                        return stage.staticallyUsedResources->count(
                                   BindingResourceKey{set, bind, resourceKind ? resourceKind : ""}) > 0;
                    }
                    if (stage.staticallyUsedNames && !stage.staticallyUsedNames->empty())
                    {
                        std::string resourceName = compiler.get_name(resourceId);
                        if (!resourceName.empty())
                            return stage.staticallyUsedNames->count(resourceName) > 0;
                    }
                    return stage.staticallyUsed && stage.staticallyUsed->count(BindingKey{set, bind}) > 0;
                };

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
                        errMsg = "auto-layout: binding type conflict across stages (different resource kind)";
                        return false;
                    }

                    // Per W3C §10.3.6 each entry point computes its own per-stage
                    // layout; the merged BGL must satisfy every stage that
                    // statically uses the binding. Where a strictly-less-permissive
                    // value would reject a valid stage, promote to the more
                    // permissive one. Otherwise, mismatches are unreconcilable
                    // and the user must supply an explicit layout.

                    if (slot.hasTexture)
                    {
                        // sampleType: float and unfilterable-float are
                        // sub-and-superset on the bind side — float requires a
                        // filterable texture (and admits unfilterable behaviour
                        // via a non-filtering sampler), unfilterable-float forbids
                        // filterable bindings. If any stage statically samples
                        // with a sampler we MUST be float, even if other stages
                        // only textureLoad.
                        const auto a = slot.texture.sampleType;
                        const auto b = incoming.texture.sampleType;
                        if (a != b)
                        {
                            const bool floatPair =
                                (a == WGPUTextureSampleType_Float &&
                                 b == WGPUTextureSampleType_UnfilterableFloat) ||
                                (a == WGPUTextureSampleType_UnfilterableFloat &&
                                 b == WGPUTextureSampleType_Float);
                            if (floatPair)
                            {
                                slot.texture.sampleType = WGPUTextureSampleType_Float;
                            }
                            else
                            {
                                errMsg = "auto-layout: binding sampleType conflict across stages";
                                return false;
                            }
                        }
                        if (slot.texture.viewDimension != incoming.texture.viewDimension ||
                            slot.texture.multisampled != incoming.texture.multisampled)
                        {
                            errMsg = "auto-layout: binding texture view-dimension or "
                                     "multisampled conflict across stages";
                            return false;
                        }
                    }
                    if (slot.hasBuffer)
                    {
                        // type: storage covers read-only-storage; uniform never mixes.
                        const auto a = slot.buffer.type;
                        const auto b = incoming.buffer.type;
                        if (a != b)
                        {
                            const bool storagePair =
                                (a == WGPUBufferBindingType_Storage &&
                                 b == WGPUBufferBindingType_ReadOnlyStorage) ||
                                (a == WGPUBufferBindingType_ReadOnlyStorage &&
                                 b == WGPUBufferBindingType_Storage);
                            if (storagePair)
                            {
                                slot.buffer.type = WGPUBufferBindingType_Storage;
                            }
                            else
                            {
                                errMsg = "auto-layout: binding buffer-type conflict across stages";
                                return false;
                            }
                        }
                    }
                    if (slot.hasStorageTexture)
                    {
                        // access: read-write covers read-only and write-only;
                        // any mix promotes to read-write so both load and store
                        // sides remain valid.
                        const auto a = slot.storageTexture.access;
                        const auto b = incoming.storageTexture.access;
                        if (a != b)
                            slot.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
                        if (slot.storageTexture.format != incoming.storageTexture.format ||
                            slot.storageTexture.viewDimension !=
                                incoming.storageTexture.viewDimension)
                        {
                            errMsg = "auto-layout: binding storage-texture format or "
                                     "view-dimension conflict across stages";
                            return false;
                        }
                    }
                    if (slot.hasSampler)
                    {
                        if (slot.sampler.type != incoming.sampler.type)
                        {
                            errMsg = "auto-layout: binding sampler-type conflict across stages";
                            return false;
                        }
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
                    if (!isStaticallyUsed(r.id, "uniform-buffer"))
                        continue;
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
                    if (!isStaticallyUsed(r.id, "storage-buffer"))
                        continue;
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
                    if (!isStaticallyUsed(r.id, "texture"))
                        continue;
                    uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                    uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                    const auto &t = compiler.get_type(r.type_id);
                    ReflectedBinding rb = buildBase(set, bind);
                    rb.hasTexture = true;
                    const bool paired = sampledImageVarIds.count(r.id) > 0;
                    rb.texture.sampleType = SampleTypeFromSpirv(compiler, t, paired);
                    rb.texture.viewDimension = DimensionFromSpirv(t);
                    rb.texture.multisampled = t.image.ms ? 1u : 0u;
                    if (!mergeInto(set, bind, rb))
                        return false;
                }

                // Combined image samplers (HLSL->SPIR-V via DXC often emits these)
                // — always sampler-paired by construction.
                for (const auto &r : res.sampled_images)
                {
                    if (!isStaticallyUsed(r.id, "texture"))
                        continue;
                    uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                    uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
                    const auto &t = compiler.get_type(r.type_id);
                    ReflectedBinding rb = buildBase(set, bind);
                    rb.hasTexture = true;
                    rb.texture.sampleType = SampleTypeFromSpirv(compiler, t, true);
                    rb.texture.viewDimension = DimensionFromSpirv(t);
                    rb.texture.multisampled = t.image.ms ? 1u : 0u;
                    if (!mergeInto(set, bind, rb))
                        return false;
                }

                // Separate samplers
                for (const auto &r : res.separate_samplers)
                {
                    if (!isStaticallyUsed(r.id, "sampler"))
                        continue;
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
                    if (!isStaticallyUsed(r.id, "storage-texture"))
                        continue;
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
                    rb.storageTexture.format =
                        StorageFormatForBinding(stage, set, bind,
                                                StorageFormatFromSpirv(t.image.format));
                    rb.storageTexture.viewDimension = DimensionFromSpirv(t);
                    if (!mergeInto(set, bind, rb))
                        return false;
                }

                return true;
            }
            catch (const std::exception &e)
            {
                errMsg = std::string("auto-layout: spirv-cross exception: ") + e.what();
                return false;
            }
            catch (...)
            {
                errMsg = "auto-layout: unknown spirv-cross exception";
                return false;
            }
        }
    } // namespace

    static uint64_t RoundUp(uint64_t alignment, uint64_t value)
    {
        if (alignment == 0)
            return value;
        return ((value + alignment - 1ull) / alignment) * alignment;
    }

    static uint64_t ComputeScalarByteSize(const spirv_cross::SPIRType &type)
    {
        switch (type.basetype)
        {
        case spirv_cross::SPIRType::Boolean:
            return 4;
        case spirv_cross::SPIRType::Char:
        case spirv_cross::SPIRType::SByte:
        case spirv_cross::SPIRType::UByte:
            return 1;
        case spirv_cross::SPIRType::Short:
        case spirv_cross::SPIRType::UShort:
        case spirv_cross::SPIRType::Half:
            return 2;
        case spirv_cross::SPIRType::Int:
        case spirv_cross::SPIRType::UInt:
        case spirv_cross::SPIRType::Float:
            return 4;
        case spirv_cross::SPIRType::Int64:
        case spirv_cross::SPIRType::UInt64:
        case spirv_cross::SPIRType::Double:
            return 8;
        default:
            return 0;
        }
    }

    static uint64_t ComputeVectorAlignment(const spirv_cross::SPIRType &type)
    {
        uint64_t scalarAlign = ComputeScalarByteSize(type);
        if (type.vecsize == 1)
            return scalarAlign;
        if (type.vecsize == 2)
            return scalarAlign * 2ull;
        return scalarAlign * 4ull;
    }

    static uint64_t ComputeTypeSize(const spirv_cross::Compiler &compiler, uint32_t typeId);
    static uint64_t ComputeTypeAlignment(const spirv_cross::Compiler &compiler, uint32_t typeId);

    static uint64_t ComputeNonArrayTypeAlignment(const spirv_cross::Compiler &compiler,
                                                 const spirv_cross::SPIRType &type);

    static uint64_t ComputeStructSize(const spirv_cross::Compiler &compiler,
                                      uint32_t typeId,
                                      const spirv_cross::SPIRType &type)
    {
        uint64_t structAlign = ComputeNonArrayTypeAlignment(compiler, type);
        uint64_t structSize = 0;
        for (uint32_t i = 0; i < type.member_types.size(); ++i)
        {
            uint64_t memberAlign = ComputeTypeAlignment(compiler, type.member_types[i]);
            uint64_t memberOffset = RoundUp(memberAlign, structSize);
            if (compiler.has_member_decoration(typeId, i, spv::DecorationOffset))
                memberOffset = compiler.get_member_decoration(typeId, i, spv::DecorationOffset);

            uint64_t memberSize = ComputeTypeSize(compiler, type.member_types[i]);
            uint64_t memberEnd = memberOffset + memberSize;
            if (memberEnd > structSize)
                structSize = memberEnd;
        }
        return RoundUp(structAlign, structSize);
    }

    static uint64_t ComputeNonArrayTypeSize(const spirv_cross::Compiler &compiler,
                                            uint32_t typeId,
                                            const spirv_cross::SPIRType &type)
    {
        if (type.basetype == spirv_cross::SPIRType::Struct)
            return ComputeStructSize(compiler, typeId, type);

        uint64_t scalarOrVectorSize = ComputeScalarByteSize(type) * type.vecsize;
        if (type.columns <= 1)
            return scalarOrVectorSize;

        uint64_t columnStride = RoundUp(ComputeVectorAlignment(type), scalarOrVectorSize);
        return columnStride * type.columns;
    }

    static uint64_t ComputeNonArrayTypeAlignment(const spirv_cross::Compiler &compiler,
                                                 const spirv_cross::SPIRType &type)
    {
        if (type.basetype == spirv_cross::SPIRType::Struct)
        {
            uint64_t align = 1;
            for (uint32_t memberTypeId : type.member_types)
            {
                uint64_t memberAlign = ComputeTypeAlignment(compiler, memberTypeId);
                if (memberAlign > align)
                    align = memberAlign;
            }
            return align;
        }

        return ComputeVectorAlignment(type);
    }

    static uint64_t ComputeTypeSize(const spirv_cross::Compiler &compiler, uint32_t typeId)
    {
        const auto &type = compiler.get_type(typeId);
        if (!type.array.empty())
        {
            uint64_t count = type.array.back();
            if (compiler.has_decoration(typeId, spv::DecorationArrayStride))
                return static_cast<uint64_t>(compiler.get_decoration(typeId, spv::DecorationArrayStride)) * count;
            if (type.parent_type != 0)
            {
                uint64_t elementSize = ComputeTypeSize(compiler, type.parent_type);
                uint64_t elementAlign = ComputeTypeAlignment(compiler, type.parent_type);
                return RoundUp(elementAlign, elementSize) * count;
            }
            uint64_t elementSize = ComputeNonArrayTypeSize(compiler, typeId, type);
            uint64_t elementAlign = ComputeNonArrayTypeAlignment(compiler, type);
            return RoundUp(elementAlign, elementSize) * count;
        }
        return ComputeNonArrayTypeSize(compiler, typeId, type);
    }

    static uint64_t ComputeTypeAlignment(const spirv_cross::Compiler &compiler, uint32_t typeId)
    {
        const auto &type = compiler.get_type(typeId);
        if (!type.array.empty())
        {
            if (type.parent_type != 0)
                return ComputeTypeAlignment(compiler, type.parent_type);
            return ComputeNonArrayTypeAlignment(compiler, type);
        }
        return ComputeNonArrayTypeAlignment(compiler, type);
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
        // Same crash class as ReflectOneStage — spirv-cross throws on
        // missing entry point and unwinds through extern "C". Catch here.
        std::unique_ptr<spirv_cross::Compiler> compilerPtr;
        try
        {
            compilerPtr = std::make_unique<spirv_cross::Compiler>(spirv.data(), spirv.size());
            if (!entryPoint.empty())
                compilerPtr->set_entry_point(entryPoint, spv::ExecutionModelGLCompute);
        }
        catch (const std::exception &e)
        {
            errMsg = std::string("GetComputeWorkgroupInfo: spirv-cross rejected entry point '") +
                     entryPoint + "': " + e.what();
            return false;
        }
        spirv_cross::Compiler &compiler = *compilerPtr;
        // get_execution_mode_argument / get_type can also throw on malformed
        // SPIR-V; catch here so the unwind doesn't cross extern "C".
        try
        {

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
                            uint64_t sz = ComputeTypeSize(compiler, ptrType.parent_type);
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
        catch (const std::exception &e)
        {
            errMsg = std::string("GetComputeWorkgroupInfo: spirv-cross exception: ") + e.what();
            return false;
        }
        catch (...)
        {
            errMsg = "GetComputeWorkgroupInfo: unknown spirv-cross exception";
            return false;
        }
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
                                     const char *resourceKind,
                                     auto typeCheck) -> std::string
                {
                    for (const auto &r : list)
                    {
                        uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                        uint32_t binding = compiler.get_decoration(r.id, spv::DecorationBinding);
                        // Filter only when authoritative. Empty-as-fallback
                        // here would silently skip every binding when the
                        // per-EP set is missing/empty for an EP that does use
                        // bindings — a false-positive validation success.
                        if (stageIn.staticallyUsedAuthoritative)
                        {
                            if (stageIn.staticallyUsedResources &&
                                stageIn.staticallyUsedResources->count(BindingResourceKey{
                                    set, binding, resourceKind ? resourceKind : ""}) == 0)
                                continue;
                            if (stageIn.staticallyUsedNames)
                            {
                                std::string resourceName = compiler.get_name(r.id);
                                if (!resourceName.empty() &&
                                    stageIn.staticallyUsedNames->count(resourceName) == 0)
                                    continue;
                            }
                            if (!stageIn.staticallyUsed ||
                                stageIn.staticallyUsed->count(BindingKey{set, binding}) == 0)
                                continue;
                        }
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
                    uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                    uint32_t binding = compiler.get_decoration(r.id, spv::DecorationBinding);
                    WGPUTextureFormat wgslFormat =
                        StorageFormatForBinding(stageIn, set, binding,
                                                StorageFormatFromSpirv(t.image.format));
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
                if (!(err = checkList(res.uniform_buffers, "uniform-buffer", isUniformBuf)).empty())
                    return err;
                if (!(err = checkList(res.storage_buffers, "storage-buffer", isStorageBuf)).empty())
                    return err;
                if (!(err = checkList(res.separate_samplers, "sampler", isSampler)).empty())
                    return err;
                if (!(err = checkList(res.separate_images, "texture", isSampledTex)).empty())
                    return err;
                if (!(err = checkList(res.sampled_images, "texture", isSampledTex)).empty())
                    return err;
                if (!(err = checkList(res.storage_images, "storage-texture", isStorageTex)).empty())
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

        // Pre-pass: per-stage resource counters and per-binding-index check.
        // Auto-layout still has to honor maxXxxPerShaderStage and
        // maxBindingsPerBindGroup since the pipeline layout it produces is
        // logically equivalent to an explicit one (W3C §3.6.2). Validate
        // before allocating any Vulkan handles so failure leaks nothing.
        {
            struct StageCounters
            {
                uint32_t uniformBuffers = 0;
                uint32_t storageBuffers = 0;
                uint32_t samplers = 0;
                uint32_t sampledTextures = 0;
                uint32_t storageTextures = 0;
            };
            StageCounters vertexCounters{}, fragmentCounters{}, computeCounters{};
            const auto &lim = device->limits;
            for (const auto &setKv : merged)
            {
                if (setKv.first >= lim.maxBindGroups)
                {
                    errMsg = "auto-layout: bind group index >= maxBindGroups";
                    return nullptr;
                }
                for (const auto &kv : setKv.second)
                {
                    const ReflectedBinding &rb = kv.second;
                    if (rb.binding >= lim.maxBindingsPerBindGroup)
                    {
                        errMsg = "auto-layout: binding index >= maxBindingsPerBindGroup";
                        return nullptr;
                    }
                    auto accumulate = [&](StageCounters &c)
                    {
                        if (rb.hasBuffer)
                        {
                            if (rb.buffer.type == WGPUBufferBindingType_Uniform)
                                c.uniformBuffers++;
                            else
                                c.storageBuffers++;
                        }
                        else if (rb.hasSampler)
                            c.samplers++;
                        else if (rb.hasTexture)
                            c.sampledTextures++;
                        else if (rb.hasStorageTexture)
                            c.storageTextures++;
                    };
                    if (rb.visibility & WGPUShaderStage_Vertex)
                        accumulate(vertexCounters);
                    if (rb.visibility & WGPUShaderStage_Fragment)
                        accumulate(fragmentCounters);
                    if (rb.visibility & WGPUShaderStage_Compute)
                        accumulate(computeCounters);
                }
            }
            auto checkStage = [&](const StageCounters &c, const char *stageName) -> bool
            {
                if (c.uniformBuffers > lim.maxUniformBuffersPerShaderStage)
                {
                    errMsg = std::string("auto-layout: combined ") + stageName +
                             " stage exceeds maxUniformBuffersPerShaderStage";
                    return false;
                }
                if (c.storageBuffers > lim.maxStorageBuffersPerShaderStage)
                {
                    errMsg = std::string("auto-layout: combined ") + stageName +
                             " stage exceeds maxStorageBuffersPerShaderStage";
                    return false;
                }
                if (c.samplers > lim.maxSamplersPerShaderStage)
                {
                    errMsg = std::string("auto-layout: combined ") + stageName +
                             " stage exceeds maxSamplersPerShaderStage";
                    return false;
                }
                if (c.sampledTextures > lim.maxSampledTexturesPerShaderStage)
                {
                    errMsg = std::string("auto-layout: combined ") + stageName +
                             " stage exceeds maxSampledTexturesPerShaderStage";
                    return false;
                }
                if (c.storageTextures > lim.maxStorageTexturesPerShaderStage)
                {
                    errMsg = std::string("auto-layout: combined ") + stageName +
                             " stage exceeds maxStorageTexturesPerShaderStage";
                    return false;
                }
                return true;
            };
            if (!checkStage(vertexCounters, "vertex") ||
                !checkStage(fragmentCounters, "fragment") ||
                !checkStage(computeCounters, "compute"))
            {
                return nullptr;
            }
        }

        // Storage-texture format vs device-feature gates. Mirrors the
        // wgpuDeviceCreateBindGroupLayout path (Device.cpp:1881..1906) per
        // W3C §22.3.7. Auto-layout builds the BGL directly below, bypassing
        // wgpuDeviceCreateBindGroupLayout, so the same gates must run here
        // or pipelines using feature-gated formats slip through unchecked.
        {
            const bool tier1Storage =
                wgpuDeviceHasFeature(device, WGPUFeatureName_TextureFormatsTier1) == WGPU_TRUE;
            const bool tier2Rw =
                wgpuDeviceHasFeature(device, WGPUFeatureName_TextureFormatsTier2) == WGPU_TRUE;
            const bool bgra8Storage =
                wgpuDeviceHasFeature(device, WGPUFeatureName_BGRA8UnormStorage) == WGPU_TRUE;

            for (const auto &setKv : merged)
            {
                for (const auto &kv : setKv.second)
                {
                    const ReflectedBinding &rb = kv.second;
                    if (!rb.hasStorageTexture)
                        continue;

                    const auto fmt = rb.storageTexture.format;
                    if (!pwgpu::SupportsStorageBinding(fmt) &&
                        !(tier1Storage && pwgpu::SupportsStorageBindingTier1(fmt)))
                    {
                        errMsg = "auto-layout: storageTexture format does not support storage binding";
                        return nullptr;
                    }
                    if (fmt == WGPUTextureFormat_BGRA8Unorm && !bgra8Storage)
                    {
                        errMsg = "auto-layout: bgra8unorm storage texture requires BGRA8UnormStorage feature";
                        return nullptr;
                    }
                    if (fmt == WGPUTextureFormat_BGRA8Unorm &&
                        rb.storageTexture.access != WGPUStorageTextureAccess_WriteOnly)
                    {
                        errMsg = "auto-layout: bgra8unorm storage texture requires write-only access";
                        return nullptr;
                    }
                    if (rb.storageTexture.access == WGPUStorageTextureAccess_ReadWrite)
                    {
                        const bool coreRw = fmt == WGPUTextureFormat_R32Uint ||
                                            fmt == WGPUTextureFormat_R32Sint ||
                                            fmt == WGPUTextureFormat_R32Float;
                        if (!coreRw && !(tier2Rw && pwgpu::SupportsReadWriteStorageTier2(fmt)))
                        {
                            errMsg = "auto-layout: storageTexture format does not support read-write access";
                            return nullptr;
                        }
                    }
                }
            }
        }

        auto *pl = new WGPUPipelineLayoutImpl();
        pl->device = device;
        device->refCount.fetch_add(1, std::memory_order_relaxed);
        pl->bindGroupLayouts.resize(numSets, nullptr);

        auto vkDev = pe::VulkanRhi::Device();
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
                pl->ownedEmptyBackendLayouts.push_back(
                    PeToBackendHandle(static_cast<VkDescriptorSetLayout>(emptyLayout)));
                vkSetLayouts[s] = emptyLayout;
                continue;
            }

            std::vector<pe::DescriptorBindingInfo> infos;
            PeShaderStageFlags stageMask{};
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
                    info.type = rb.buffer.type == WGPUBufferBindingType_Uniform           ? PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                : rb.buffer.type == WGPUBufferBindingType_ReadOnlyStorage ? PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER
                                                                                          : PE_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                }
                else if (rb.hasSampler)
                {
                    info.type = PE_DESCRIPTOR_TYPE_SAMPLER;
                }
                else if (rb.hasTexture)
                {
                    info.type = PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    info.imageLayout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                else if (rb.hasStorageTexture)
                {
                    info.type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    info.imageLayout = PE_IMAGE_LAYOUT_GENERAL;
                }
                PeShaderStageFlags entryStages{};
                if (rb.visibility & WGPUShaderStage_Vertex)
                    entryStages |= PE_SHADER_STAGE_VERTEX;
                if (rb.visibility & WGPUShaderStage_Fragment)
                    entryStages |= PE_SHADER_STAGE_FRAGMENT;
                if (rb.visibility & WGPUShaderStage_Compute)
                    entryStages |= PE_SHADER_STAGE_COMPUTE;
                stageMask |= entryStages;
                infos.push_back(info);
            }
            bgl->bindingInfos = infos;
            bgl->stage = stageMask;
            if (!infos.empty())
            {
                bgl->layout = pe::DescriptorLayout::Create(infos, stageMask, "auto_bgl");
                vkSetLayouts[s] = pe::GetVulkanDescriptorLayout(bgl->layout);
            }
            else
            {
                vk::DescriptorSetLayoutCreateInfo emptyCI{};
                auto emptyLayout = vkDev.createDescriptorSetLayout(emptyCI);
                pl->ownedEmptyBackendLayouts.push_back(
                    PeToBackendHandle(static_cast<VkDescriptorSetLayout>(emptyLayout)));
                vkSetLayouts[s] = emptyLayout;
            }
        }

        vk::PipelineLayoutCreateInfo ci{};
        ci.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
        ci.pSetLayouts = vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
        vk::PipelineLayout layout = vkDev.createPipelineLayout(ci);
        pl->backendLayout = PeToBackendHandle(static_cast<VkPipelineLayout>(layout));
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
