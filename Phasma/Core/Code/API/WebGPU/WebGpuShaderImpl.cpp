#include "API/WebGPU/WebGpuShaderImpl.h"
#include "API/Reflection.h"
#include "API/Shader.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    namespace
    {
        constexpr uint32_t kPushGroup = 3; // the backend's push-constant slot (decision 8), not an engine set

        template <typename T, size_t N>
        uint32_t Lookup(const std::pair<std::string_view, T> (&table)[N], std::string_view name)
        {
            for (const auto &[key, value] : table)
            {
                if (key == name)
                    return static_cast<uint32_t>(value);
            }

            return 0;
        }

        constexpr std::pair<std::string_view, WGPUTextureViewDimension> kViewDimensions[] = {
            {"1d", WGPUTextureViewDimension_1D},
            {"2d", WGPUTextureViewDimension_2D},
            {"2d-array", WGPUTextureViewDimension_2DArray},
            {"cube", WGPUTextureViewDimension_Cube},
            {"cube-array", WGPUTextureViewDimension_CubeArray},
            {"3d", WGPUTextureViewDimension_3D},
        };

        constexpr std::pair<std::string_view, WGPUTextureSampleType> kSampleTypes[] = {
            {"float", WGPUTextureSampleType_Float},
            {"unfilterable-float", WGPUTextureSampleType_UnfilterableFloat},
            {"depth", WGPUTextureSampleType_Depth},
            {"sint", WGPUTextureSampleType_Sint},
            {"uint", WGPUTextureSampleType_Uint},
        };

        constexpr std::pair<std::string_view, WGPUStorageTextureAccess> kStorageAccesses[] = {
            {"write-only", WGPUStorageTextureAccess_WriteOnly},
            {"read-only", WGPUStorageTextureAccess_ReadOnly},
            {"read-write", WGPUStorageTextureAccess_ReadWrite},
        };

        constexpr std::pair<std::string_view, WGPUSamplerBindingType> kSamplerTypes[] = {
            {"filtering", WGPUSamplerBindingType_Filtering},
            {"comparison", WGPUSamplerBindingType_Comparison},
        };

        // WGSL storage texel formats; the converter writes naga's names, which are WebGPU's.
        constexpr std::pair<std::string_view, WGPUTextureFormat> kStorageFormats[] = {
            {"r8unorm", WGPUTextureFormat_R8Unorm},
            {"r8snorm", WGPUTextureFormat_R8Snorm},
            {"r8uint", WGPUTextureFormat_R8Uint},
            {"r8sint", WGPUTextureFormat_R8Sint},
            {"r16uint", WGPUTextureFormat_R16Uint},
            {"r16sint", WGPUTextureFormat_R16Sint},
            {"r16float", WGPUTextureFormat_R16Float},
            {"rg8unorm", WGPUTextureFormat_RG8Unorm},
            {"rg8snorm", WGPUTextureFormat_RG8Snorm},
            {"rg8uint", WGPUTextureFormat_RG8Uint},
            {"rg8sint", WGPUTextureFormat_RG8Sint},
            {"r32uint", WGPUTextureFormat_R32Uint},
            {"r32sint", WGPUTextureFormat_R32Sint},
            {"r32float", WGPUTextureFormat_R32Float},
            {"rg16uint", WGPUTextureFormat_RG16Uint},
            {"rg16sint", WGPUTextureFormat_RG16Sint},
            {"rg16float", WGPUTextureFormat_RG16Float},
            {"rgba8unorm", WGPUTextureFormat_RGBA8Unorm},
            {"rgba8snorm", WGPUTextureFormat_RGBA8Snorm},
            {"rgba8uint", WGPUTextureFormat_RGBA8Uint},
            {"rgba8sint", WGPUTextureFormat_RGBA8Sint},
            {"bgra8unorm", WGPUTextureFormat_BGRA8Unorm},
            {"rgb10a2uint", WGPUTextureFormat_RGB10A2Uint},
            {"rgb10a2unorm", WGPUTextureFormat_RGB10A2Unorm},
            {"rg11b10ufloat", WGPUTextureFormat_RG11B10Ufloat},
            {"rg32uint", WGPUTextureFormat_RG32Uint},
            {"rg32sint", WGPUTextureFormat_RG32Sint},
            {"rg32float", WGPUTextureFormat_RG32Float},
            {"rgba16uint", WGPUTextureFormat_RGBA16Uint},
            {"rgba16sint", WGPUTextureFormat_RGBA16Sint},
            {"rgba16float", WGPUTextureFormat_RGBA16Float},
            {"rgba32uint", WGPUTextureFormat_RGBA32Uint},
            {"rgba32sint", WGPUTextureFormat_RGBA32Sint},
            {"rgba32float", WGPUTextureFormat_RGBA32Float},
        };

        // "<group> <binding> <kind> [details]"; the format is written by Phasma/WebGPU/SpvToWgsl.
        WebGpuSidecarEntry ParseSidecarLine(const std::string &line, const std::string &key)
        {
            WebGpuSidecarEntry entry{};
            std::string kind;
            std::string a;
            std::string b;
            std::string c;
            std::istringstream in(line);
            in >> entry.group >> entry.binding >> kind >> a >> b >> c;

            WebGpuBindingFacts &f = entry.facts;
            if (kind == "uniform")
            {
                f.buffer = WGPUBufferBindingType_Uniform;
            }
            else if (kind == "storage" && (a == "read" || a == "read-write"))
            {
                f.buffer = a == "read" ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
            }
            else if (kind == "texture")
            {
                f.viewDimension = Lookup(kViewDimensions, a);
                f.sampleType = Lookup(kSampleTypes, b);
                f.multisampled = c == "1";
            }
            else if (kind == "storage-texture")
            {
                f.viewDimension = Lookup(kViewDimensions, a);
                f.storageFormat = Lookup(kStorageFormats, b);
                f.storageAccess = Lookup(kStorageAccesses, c);
            }
            else if (kind == "sampler")
            {
                f.sampler = Lookup(kSamplerTypes, a);
            }

            const bool texture = f.viewDimension && f.sampleType;
            const bool storageTexture = f.viewDimension && f.storageFormat && f.storageAccess;
            const bool known = f.buffer || f.sampler || texture || storageTexture;
            PE_ERROR_IF(!known, "[WebGPU] shader %s: unreadable binding sidecar line '%s'", key.c_str(), line.c_str());
            return entry;
        }

        std::string ReadText(const std::string &path)
        {
            FileSystem file(path, std::ios::in | std::ios::binary | std::ios::ate);
            PE_ERROR_IF(!file.IsOpen(), "[WebGPU] cannot open '%s'", path.c_str());
            return file.ReadAll();
        }

        uint32_t MergeFact(uint32_t a, uint32_t b, const char *what, const std::string &bindingName)
        {
            if (a == 0)
                return b;

            PE_ERROR_IF(b != 0 && a != b,
                        "[WebGPU] binding '%s': the pass's shaders disagree on its %s (%u vs %u)",
                        bindingName.c_str(),
                        what,
                        a,
                        b);
            return a;
        }

        // Adds one sidecar line's facts to the first engine resource at (set, binding) in `list`.
        template <typename T>
        bool ApplyAt(std::vector<T> &list, int set, int binding, const WebGpuBindingFacts &facts, bool fits)
        {
            for (T &desc : list)
            {
                if (desc.set != set || desc.binding != binding)
                    continue;

                PE_ERROR_IF(!fits,
                            "[WebGPU] '%s' (set %d, binding %d): the binding sidecar has another kind of resource",
                            desc.name.c_str(),
                            set,
                            binding);
                desc.webgpu = MergeWebGpuBindingFacts(desc.webgpu, facts, desc.name);
                return true;
            }

            return false;
        }
    } // namespace

    WebGpuBindingFacts MergeWebGpuBindingFacts(const WebGpuBindingFacts &a,
                                               const WebGpuBindingFacts &b,
                                               const std::string &bindingName)
    {
        WebGpuBindingFacts merged{};
        uint32_t sampleA = a.sampleType;
        uint32_t sampleB = b.sampleType;
        constexpr uint32_t kFloat = WGPUTextureSampleType_Float;
        constexpr uint32_t kUnfilterable = WGPUTextureSampleType_UnfilterableFloat;
        const bool floatA = sampleA == kFloat || sampleA == kUnfilterable;
        const bool floatB = sampleB == kFloat || sampleB == kUnfilterable;
        if (floatA && floatB)
        {
            sampleA = kFloat;
            sampleB = kFloat;
        }

        merged.buffer = MergeFact(a.buffer, b.buffer, "buffer type", bindingName);
        merged.sampleType = MergeFact(sampleA, sampleB, "sample type", bindingName);
        merged.viewDimension = MergeFact(a.viewDimension, b.viewDimension, "view dimension", bindingName);
        merged.storageFormat = MergeFact(a.storageFormat, b.storageFormat, "storage format", bindingName);
        merged.storageAccess = MergeFact(a.storageAccess, b.storageAccess, "storage access", bindingName);
        merged.sampler = MergeFact(a.sampler, b.sampler, "sampler type", bindingName);
        merged.multisampled = a.multisampled || b.multisampled;
        return merged;
    }

    // Engine reflection comes from the SPIR-V; the layout facts come from the WGSL's sidecar (spec decision 7).
    void ApplyWebGpuBindingSidecar(Reflection &refl, Shader *shader)
    {
        const WebGpuShaderImpl *impl = WebGpuShaderImpl::TryFrom(shader);
        if (!impl)
            return;

        for (const WebGpuSidecarEntry &entry : impl->m_bindings)
        {
            if (entry.group == kPushGroup)
                continue;

            const int set = static_cast<int>(entry.group);
            const int binding = static_cast<int>(entry.binding / 2);
            const WebGpuBindingFacts &f = entry.facts;
            const bool texture = f.viewDimension && f.sampleType;
            const bool storageBuffer = f.buffer == WGPUBufferBindingType_Storage ||
                                       f.buffer == WGPUBufferBindingType_ReadOnlyStorage;

            bool applied = false;
            if (entry.binding % 2 == 1)
            {
                // Binding rule: only a combined image sampler's sampler sits at an odd binding. Reflection reports the
                // pair as one combined resource, or as an image and a sampler that GetDescriptors merges.
                applied = ApplyAt(refl.m_combinedImageSamplers, set, binding, f, f.sampler != 0) ||
                          ApplyAt(refl.m_samplers, set, binding, f, f.sampler != 0);
            }
            else
            {
                applied = ApplyAt(refl.m_combinedImageSamplers, set, binding, f, texture) ||
                          ApplyAt(refl.m_images, set, binding, f, texture) ||
                          ApplyAt(refl.m_storageImages, set, binding, f, f.storageFormat != 0) ||
                          ApplyAt(refl.m_samplers, set, binding, f, f.sampler != 0) ||
                          ApplyAt(refl.m_uniformBuffers, set, binding, f, f.buffer == WGPUBufferBindingType_Uniform) ||
                          ApplyAt(refl.m_storageBuffers, set, binding, f, storageBuffer);
            }

            PE_ERROR_IF(!applied,
                        "[WebGPU] shader '%s': sidecar binding %u.%u has no engine resource at set %d, binding %d",
                        shader->GetCache().GetSourcePath().c_str(),
                        entry.group,
                        entry.binding,
                        set,
                        binding);
        }
    }

    WebGpuShaderImpl::WebGpuShaderImpl(Shader *owner)
        : m_owner{owner}
    {
        ShaderCache &cache = owner->GetCache();
        const std::string key = std::to_string(cache.GetHash());
        const std::string wgslPath = Path::Root + "ShaderCache/_wgsl/" + key;
        const std::string sidecarPath = wgslPath + ".bindings";

        // SPIR-V without WGSL means the bake converted only part of the cache.
        const bool baked = std::filesystem::exists(wgslPath) && std::filesystem::exists(sidecarPath);
        PE_ERROR_IF(!baked,
                    "[WebGPU] no baked WGSL for '%s' (entry '%s', key %s): run tools/convert_web_shaders.py on the bake",
                    cache.GetSourcePath().c_str(),
                    owner->GetEntryName().c_str(),
                    key.c_str());

        m_spirv = cache.ReadSpvFile();
        m_wgsl = ReadText(wgslPath);

        std::istringstream lines(ReadText(sidecarPath));
        for (std::string line; std::getline(lines, line);)
        {
            if (!line.empty())
                m_bindings.push_back(ParseSidecarLine(line, key));
        }
    }

    WebGpuShaderImpl::~WebGpuShaderImpl()
    {
        if (m_module)
            wgpuShaderModuleRelease(m_module);
    }

    WGPUShaderModule WebGpuShaderImpl::GetOrCreateModule()
    {
        if (m_module)
            return m_module;

        WGPUShaderSourceWGSL source{};
        source.chain.sType = WGPUSType_ShaderSourceWGSL;
        source.code = {m_wgsl.data(), m_wgsl.size()};

        const std::string label = m_owner->GetCache().GetSourcePath() + ":" + m_owner->GetEntryName();
        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &source.chain;
        desc.label = {label.data(), label.size()};
        m_module = wgpuDeviceCreateShaderModule(WebGpuRhiImpl::Get()->m_device, &desc);
        PE_ERROR_IF(!m_module, "[WebGPU] shader module creation failed for '%s'", label.c_str());
        return m_module;
    }
} // namespace pe
