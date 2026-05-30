#include "API/Shader.h"
#include "API/Shader_Internal.h"
#include "API/Descriptor.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanShaderImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12ShaderImpl.h"
#endif

namespace pe
{
    namespace
    {
        std::vector<Shader *> &TrackedShaders()
        {
            static std::vector<Shader *> s_tracked;
            return s_tracked;
        }

        Descriptor *GetDescriptorSafely(const std::vector<Descriptor *> &descriptors, size_t index)
        {
            return (index < descriptors.size()) ? descriptors[index] : nullptr;
        }

        Descriptor *MergeDescriptors(Descriptor *a, Descriptor *b)
        {
            if (!a)
                return b;
            if (!b)
                return a;
            if (a == b)
                return a;

            std::vector<DescriptorBindingInfo> mergedBindings = a->GetBindingInfos();
            const auto &bBindings = b->GetBindingInfos();
            for (const auto &bInfo : bBindings)
            {
                bool found = false;
                for (auto &aInfo : mergedBindings)
                {
                    if (aInfo.binding == bInfo.binding)
                    {
                        found = true;
                        PE_ERROR_IF(aInfo.type != bInfo.type, "Descriptor binding type mismatch during merge");
                        PE_ERROR_IF(aInfo.dxRegister != bInfo.dxRegister || aInfo.dxSpace != bInfo.dxSpace,
                                    "Descriptor binding DX12 register mismatch during merge");
                        PE_ERROR_IF(aInfo.structuredStride != bInfo.structuredStride,
                                    "Descriptor binding structured stride mismatch during merge");
                        aInfo.count = std::max(aInfo.count, bInfo.count);
                        if (bInfo.bindless && !aInfo.bindless)
                            aInfo.bindless = true;
                        break;
                    }
                }
                if (!found)
                    mergedBindings.push_back(bInfo);
            }

            PeShaderStageFlags mergedStage = a->GetStage() | b->GetStage();
            bool pushDesc = a->GetLayout()->IsPushDescriptor() || b->GetLayout()->IsPushDescriptor();
            Descriptor *merged = Descriptor::Create(mergedBindings, mergedStage, pushDesc, "merged_descriptor");

            Descriptor::Destroy(a);
            Descriptor::Destroy(b);
            return merged;
        }

        std::vector<Descriptor *> CombineDescriptors(const std::vector<Descriptor *> &a, const std::vector<Descriptor *> &b)
        {
            std::vector<Descriptor *> descriptors;
            const size_t maxSize = std::max(a.size(), b.size());
            descriptors.reserve(maxSize);
            for (size_t i = 0; i < maxSize; ++i)
            {
                auto *descA = GetDescriptorSafely(a, i);
                auto *descB = GetDescriptorSafely(b, i);
                descriptors.push_back(MergeDescriptors(descA, descB));
            }
            return descriptors;
        }

        void ValidateShaderStage(Shader *shader, PeShaderStageFlags expectedStage)
        {
            PE_ERROR_IF(shader->GetShaderStage() != expectedStage, "Invalid shader stage");
        }
    } // namespace

    Shader::Impl *CreateShaderImpl(Shader *owner, const ShaderDesc &desc, bool needsCompile)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            if (needsCompile)
                return new Dx12ShaderImpl(owner, desc);

            std::vector<uint8_t> cached = owner->GetCache().ReadBytecodeFile();
            return new Dx12ShaderImpl(owner, cached);
#else
            PE_ERROR("[Shader] DX12 shader creation is Windows-only");
            return nullptr;
#endif
        }

        if (needsCompile)
            return new VulkanShaderImpl(owner, desc);

        std::vector<uint32_t> cached = owner->GetCache().ReadSpvFile();
        return new VulkanShaderImpl(owner, cached.data(), cached.size());
    }

    Shader::Shader() = default;
    Shader::~Shader()
    {
        delete m_impl;
        m_impl = nullptr;
    }

    Shader *Shader::Create(const ShaderDesc &desc)
    {
        Shader *shader = new Shader();
        shader->m_entryName = desc.entryPoint;
        shader->m_stage = desc.stage;
        shader->m_localDefines = desc.defines;
        shader->m_type = desc.type;

        std::error_code ec;
        const std::string path = std::filesystem::weakly_canonical(desc.sourcePath, ec).generic_string();

        PE_ERROR_IF(!std::filesystem::exists(path), "[Shader] Source file does not exist: %s", desc.sourcePath.c_str());
        if (!FileWatcher::Get(path))
        {
            FileWatcher::Add(path, [](size_t fileEvent)
                             { EventSystem::PushEvent(EventType::CompileShaders, fileEvent); });
        }
        shader->m_pathID = StringHash(path);

        Hash definesHash;
        for (const Define &def : m_globalDefines)
        {
            definesHash.CombineString(def.name);
            definesHash.CombineString(def.value);
        }
        for (const Define &def : desc.defines)
        {
            definesHash.CombineString(def.name);
            definesHash.CombineString(def.value);
        }
        definesHash.CombineValue(static_cast<uint32_t>(desc.stage));

        shader->m_cache.Init(path, shader->m_entryName, definesHash);
        const bool needsCompile = shader->m_cache.ShaderNeedsCompile();
        if (needsCompile)
        {
            PE_INFO("[Shader] Compile(hlsl) -> source='%s' | path='%s' | hash=%llu",
                    desc.sourcePath.c_str(), path.c_str(), static_cast<unsigned long long>(shader->m_pathID));
        }

        shader->m_impl = CreateShaderImpl(shader, desc, needsCompile);
        shader->m_reflection.Init(shader);

        TrackedShaders().push_back(shader);
        return shader;
    }

    Shader *Shader::CreateFromBytecode(const ShaderBytecodeDesc &desc)
    {
        Shader *shader = new Shader();
        shader->m_entryName = desc.entryPoint;
        shader->m_stage = desc.stage;
        shader->m_type = ShaderCodeType::HLSL;
        shader->m_reflectionSource = desc.reflectionSource;

        const std::string debugName = desc.debugName.empty() ? "direct_bytecode_shader" : desc.debugName;
        shader->m_pathID = StringHash(debugName);
        PE_INFO("[Shader] Init(bytecode) -> name='%s' | hash=%llu",
                debugName.c_str(), static_cast<unsigned long long>(shader->m_pathID));

        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            PE_ERROR_IF(!desc.dxil || desc.dxilSizeBytes == 0,
                        "[Shader] Direct DX12 shader '%s' has no DXIL bytecode",
                        debugName.c_str());
            std::vector<uint8_t> dxil(desc.dxil, desc.dxil + desc.dxilSizeBytes);
            shader->m_impl = new Dx12ShaderImpl(shader, std::move(dxil));
#else
            PE_ERROR("[Shader] DX12 bytecode shader creation is Windows-only");
#endif
        }
        else
        {
            PE_ERROR_IF(!desc.spirv || desc.spirvSizeBytes == 0 || (desc.spirvSizeBytes % sizeof(uint32_t)) != 0,
                        "[Shader] Direct Vulkan shader '%s' has invalid SPIR-V bytecode",
                        debugName.c_str());
            const size_t wordCount = desc.spirvSizeBytes / sizeof(uint32_t);
            std::vector<uint32_t> spirv(wordCount);
            memcpy(spirv.data(), desc.spirv, desc.spirvSizeBytes);
            shader->m_impl = new VulkanShaderImpl(shader, spirv.data(), spirv.size());
        }

        shader->m_reflection.Init(shader);
        TrackedShaders().push_back(shader);
        return shader;
    }

    void Shader::Destroy(Shader *&shader)
    {
        if (!shader)
            return;

        auto &tracked = TrackedShaders();
        tracked.erase(std::remove(tracked.begin(), tracked.end(), shader), tracked.end());
        delete shader;
        shader = nullptr;
    }

    std::vector<Shader *> Shader::GetHandles()
    {
        return TrackedShaders();
    }

    void Shader::AddGlobalDefine(const std::string &name, const std::string &value)
    {
        for (auto &def : m_globalDefines)
        {
            if (def.name == name)
            {
                def.value = value;
                return;
            }
        }
        m_globalDefines.push_back({name, value});
    }

    std::vector<Descriptor *> Shader::ReflectPassDescriptors(const PassInfo &passInfo)
    {
        Shader *comp = passInfo.pCompShader;
        Shader *vert = passInfo.pVertShader;
        Shader *frag = passInfo.pFragShader;
        Shader *rayGen = passInfo.acceleration.rayGen;
        const auto &miss = passInfo.acceleration.miss;
        const auto &hitGroups = passInfo.acceleration.hitGroups;

        if (rayGen)
        {
            std::vector<Descriptor *> descriptors;
            {
                auto desc = rayGen->GetReflection().GetDescriptors();
                descriptors = CombineDescriptors(descriptors, desc);
            }
            for (auto *shader : miss)
            {
                auto desc = shader->GetReflection().GetDescriptors();
                descriptors = CombineDescriptors(descriptors, desc);
            }
            for (auto &hitGroup : hitGroups)
            {
                if (hitGroup.closestHit)
                {
                    auto desc = hitGroup.closestHit->GetReflection().GetDescriptors();
                    descriptors = CombineDescriptors(descriptors, desc);
                }
                if (hitGroup.anyHit)
                {
                    auto desc = hitGroup.anyHit->GetReflection().GetDescriptors();
                    descriptors = CombineDescriptors(descriptors, desc);
                }
                if (hitGroup.intersection)
                {
                    auto desc = hitGroup.intersection->GetReflection().GetDescriptors();
                    descriptors = CombineDescriptors(descriptors, desc);
                }
            }
            return descriptors;
        }
        if (vert && frag)
        {
            ValidateShaderStage(vert, PE_SHADER_STAGE_VERTEX);
            ValidateShaderStage(frag, PE_SHADER_STAGE_FRAGMENT);
            auto vertDesc = vert->GetReflection().GetDescriptors();
            auto fragDesc = frag->GetReflection().GetDescriptors();
            return CombineDescriptors(vertDesc, fragDesc);
        }
        if (vert)
        {
            ValidateShaderStage(vert, PE_SHADER_STAGE_VERTEX);
            return vert->GetReflection().GetDescriptors();
        }
        if (frag)
        {
            ValidateShaderStage(frag, PE_SHADER_STAGE_FRAGMENT);
            return frag->GetReflection().GetDescriptors();
        }
        if (comp)
        {
            ValidateShaderStage(comp, PE_SHADER_STAGE_COMPUTE);
            return comp->GetReflection().GetDescriptors();
        }

        PE_ERROR("[Shader] This state is not used in the current implementation");
        return {};
    }
} // namespace pe
