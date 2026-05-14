#include "API/Vulkan/VulkanShaderImpl.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "shaderc/shaderc.hpp"
#if defined(PE_WIN32)
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#endif
#include "dxcapi.h"

namespace pe
{
    namespace
    {
        class FileIncluder : public shaderc::CompileOptions::IncluderInterface
        {
        public:
            shaderc_include_result *GetInclude(const char *requested_source,
                                               shaderc_include_type,
                                               const char *requesting_source,
                                               size_t) override;
            void ReleaseInclude(shaderc_include_result *include_result) override;

        private:
            class FileInfo
            {
            public:
                const std::string full_path;
                std::string contents;
            };
            std::unordered_set<std::string> included_files;
        };

        shaderc_include_result *MakeErrorIncludeResult(const char *message)
        {
            return new shaderc_include_result{"", 0, message, strlen(message)};
        }

        shaderc_include_result *FileIncluder::GetInclude(const char *requested_source,
                                                         shaderc_include_type,
                                                         const char *requesting_source,
                                                         size_t)
        {
            std::filesystem::path requesting_source_path(requesting_source);

            std::string full_path;
            if (requesting_source_path.is_relative())
            {
                full_path = std::filesystem::current_path().string() + "\\" +
                            requesting_source_path.parent_path().string() + "\\" +
                            requested_source;
            }
            else
            {
                full_path = requesting_source_path.parent_path().string() + "\\" + requested_source;
            }

            if (full_path.empty())
                return MakeErrorIncludeResult("Cannot find or open include file.");

            FileInfo *file_info = new FileInfo{
                full_path,
                FileSystem(full_path, std::ios_base::in | std::ios_base::ate | std::ios::binary).ReadAll()};

            included_files.insert(full_path);

            shaderc_include_result *inlc_result = new shaderc_include_result();
            inlc_result->source_name = file_info->full_path.data();
            inlc_result->source_name_length = file_info->full_path.length();
            inlc_result->content = file_info->contents.data();
            inlc_result->content_length = file_info->contents.size();
            inlc_result->user_data = file_info;

            return inlc_result;
        }

        void FileIncluder::ReleaseInclude(shaderc_include_result *include_result)
        {
            FileInfo *info = static_cast<FileInfo *>(include_result->user_data);
            delete info;
            delete include_result;
        }

        std::wstring ConvertUtf8ToWide(const std::string &str)
        {
#ifdef _WIN32
            if (str.empty())
                return std::wstring();

            int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
            if (size_needed <= 0)
                return std::wstring();

            std::wstring wide_str(size_needed - 1, 0); // -1 to remove null terminator
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wide_str[0], size_needed);
            return wide_str;
#else
            if (str.empty())
                return std::wstring();

            std::mbstate_t state = std::mbstate_t();
            const char *src = str.c_str();
            std::size_t len = std::mbsrtowcs(nullptr, &src, 0, &state);
            if (len == static_cast<std::size_t>(-1))
                return std::wstring();

            std::vector<wchar_t> buffer(len + 1);
            std::mbsrtowcs(buffer.data(), &src, buffer.size(), &state);
            return std::wstring(buffer.data());
#endif
        }

        void PrintDxcError(IDxcResult *result)
        {
            IDxcBlobUtf8 *pErrors = nullptr;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
            if (pErrors && pErrors->GetStringLength() > 0)
                PE_INFO(pErrors->GetStringPointer());
            if (pErrors)
                pErrors->Release();
        }

        void AddDefineGlsl(const Define &def, shaderc::CompileOptions &options)
        {
            if (def.name.empty())
                return;
            if (def.value.empty())
                options.AddMacroDefinition(def.name);
            else
                options.AddMacroDefinition(def.name, def.value);
        }

        bool CompileGlsl(Shader *owner,
                         PeShaderStageFlags stage,
                         const std::vector<Define> &globalDefines,
                         const std::vector<Define> &localDefines,
                         std::vector<uint32_t> &outSpirv)
        {
            shaderc::Compiler compiler;
            shaderc::CompileOptions options;
            options.SetIncluder(std::make_unique<FileIncluder>());
            options.SetOptimizationLevel(shaderc_optimization_level_performance);

            for (const Define &def : globalDefines)
                AddDefineGlsl(def, options);
            for (const Define &def : localDefines)
                AddDefineGlsl(def, options);

#if PE_DEBUG_MODE
            options.SetGenerateDebugInfo();
#endif

            uint32_t kind = 0;
            if (stage == PE_SHADER_STAGE_VERTEX)
                kind = shaderc_vertex_shader;
            else if (stage == PE_SHADER_STAGE_FRAGMENT)
                kind = shaderc_fragment_shader;
            else if (stage == PE_SHADER_STAGE_COMPUTE)
                kind = shaderc_compute_shader;
            else if (stage == PE_SHADER_STAGE_RAYGEN_KHR)
                kind = shaderc_raygen_shader;
            else if (stage == PE_SHADER_STAGE_ANY_HIT_KHR)
                kind = shaderc_anyhit_shader;
            else if (stage == PE_SHADER_STAGE_CLOSEST_HIT_KHR)
                kind = shaderc_closesthit_shader;
            else if (stage == PE_SHADER_STAGE_MISS_KHR)
                kind = shaderc_miss_shader;
            else if (stage == PE_SHADER_STAGE_INTERSECTION_KHR)
                kind = shaderc_intersection_shader;
            else if (stage == PE_SHADER_STAGE_CALLABLE_KHR)
                kind = shaderc_callable_shader;
            else
            {
                PE_ERROR("[Shader] Invalid shader stage!");
                return false;
            }

            shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
                owner->GetCache().GetShaderCode(),
                static_cast<shaderc_shader_kind>(kind),
                owner->GetCache().GetSourcePath().c_str(),
                owner->GetEntryName().c_str(),
                options);

            if (module.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                std::cerr << module.GetErrorMessage();
                return false;
            }

            outSpirv = {module.cbegin(), module.cend()};
            return true;
        }

        bool CompileHlsl(Shader *owner,
                         PeShaderStageFlags stage,
                         const std::vector<Define> &globalDefines,
                         const std::vector<Define> &localDefines,
                         std::vector<uint32_t> &outSpirv)
        {
            std::vector<LPCWSTR> args{};
            args.push_back(L"-spirv");
            args.push_back(RHII.GetCaps().spirvTargetVulkanVersion >= VK_API_VERSION_1_3
                               ? L"-fspv-target-env=vulkan1.3"
                               : L"-fspv-target-env=vulkan1.2");

            const bool isLibraryTarget =
                (stage & (PE_SHADER_STAGE_RAYGEN_KHR | PE_SHADER_STAGE_ANY_HIT_KHR | PE_SHADER_STAGE_CLOSEST_HIT_KHR |
                          PE_SHADER_STAGE_MISS_KHR | PE_SHADER_STAGE_INTERSECTION_KHR | PE_SHADER_STAGE_CALLABLE_KHR)) != 0;

            args.push_back(L"-T");
            if (stage == PE_SHADER_STAGE_VERTEX)
                args.push_back(L"vs_6_3");
            else if (stage == PE_SHADER_STAGE_FRAGMENT)
                args.push_back(L"ps_6_3");
            else if (stage == PE_SHADER_STAGE_COMPUTE)
                args.push_back(L"cs_6_3");
            else if (isLibraryTarget)
                args.push_back(L"lib_6_3");
            else
            {
                PE_ERROR("[Shader] Invalid shader stage!");
                return false;
            }

            args.push_back(L"-fspv-preserve-bindings");
            args.push_back(L"-fspv-preserve-interface");

            // Library targets export every [shader(...)] function;
            // passing -E with lib_6_* is rejected by DXC.
            std::wstring entryName = ConvertUtf8ToWide(owner->GetEntryName());
            if (!isLibraryTarget)
            {
                args.push_back(L"-E");
                args.push_back(entryName.c_str());
            }

            args.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);
            args.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

#if PE_DEBUG_MODE
            args.push_back(DXC_ARG_DEBUG);
#endif

            std::vector<std::wstring> wdefines(globalDefines.size() + localDefines.size());
            uint32_t i = 0;
            for (const Define &def : globalDefines)
            {
                wdefines[i] += ConvertUtf8ToWide(def.name);
                if (!def.value.empty())
                    wdefines[i] += ConvertUtf8ToWide("=" + def.value);
                args.push_back(L"-D");
                args.push_back(wdefines[i++].c_str());
            }
            for (const Define &def : localDefines)
            {
                wdefines[i] += ConvertUtf8ToWide(def.name);
                if (!def.value.empty())
                    wdefines[i] += ConvertUtf8ToWide("=" + def.value);
                args.push_back(L"-D");
                args.push_back(wdefines[i++].c_str());
            }

            IDxcUtils *dxc_utils = nullptr;
            HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils));
            if (FAILED(hr))
            {
                PE_INFO("Failed to create IDxcUtils");
                return false;
            }

            IDxcIncludeHandler *include_handler = nullptr;
            hr = dxc_utils->CreateDefaultIncludeHandler(&include_handler);
            if (FAILED(hr))
            {
                PE_INFO("Failed to create IDxcIncludeHandler");
                return false;
            }

            IDxcCompiler3 *dxc_compiler = nullptr;
            hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc_compiler));
            if (FAILED(hr))
            {
                PE_INFO("Failed to create IDxcCompiler3");
                return false;
            }

            IDxcUtils *pUtils = nullptr;
            DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils));
            IDxcBlobEncoding *pSource = nullptr;
            uint32_t srcSize = static_cast<uint32_t>(owner->GetCache().GetShaderCode().size());
            pUtils->CreateBlob(owner->GetCache().GetShaderCode().c_str(), srcSize, CP_UTF8, &pSource);

            DxcBuffer sourceBuffer;
            sourceBuffer.Ptr = pSource->GetBufferPointer();
            sourceBuffer.Size = pSource->GetBufferSize();
            sourceBuffer.Encoding = DXC_CP_UTF8;

            IDxcResult *result = nullptr;
            hr = dxc_compiler->Compile(&sourceBuffer,
                                       args.data(),
                                       static_cast<uint32_t>(args.size()),
                                       include_handler,
                                       IID_PPV_ARGS(&result));

            if (FAILED(hr) || !result)
            {
                if (result)
                    PrintDxcError(result);
                PE_ERROR("[Shader] dxc_compiler->Compile failed (hr=0x%08lx) for '%s'",
                         static_cast<unsigned long>(hr),
                         owner->GetCache().GetSourcePath().c_str());
                return false;
            }

            HRESULT compileStatus;
            result->GetStatus(&compileStatus);
            if (FAILED(compileStatus))
            {
                PrintDxcError(result);
                return false;
            }

            IDxcBlob *pObject = nullptr;
            result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pObject), nullptr);
            if (!pObject)
            {
                PrintDxcError(result);
                return false;
            }

            outSpirv.resize(pObject->GetBufferSize() / sizeof(uint32_t));
            memcpy(outSpirv.data(), pObject->GetBufferPointer(), pObject->GetBufferSize());

            dxc_utils->Release();
            include_handler->Release();
            dxc_compiler->Release();
            pUtils->Release();
            pSource->Release();
            result->Release();
            pObject->Release();
            return true;
        }
    } // namespace

    vk::ShaderStageFlagBits ToVkSingleShaderStage(PeShaderStageFlags stage)
    {
        if (stage == PE_SHADER_STAGE_VERTEX)
            return vk::ShaderStageFlagBits::eVertex;
        if (stage == PE_SHADER_STAGE_FRAGMENT)
            return vk::ShaderStageFlagBits::eFragment;
        if (stage == PE_SHADER_STAGE_COMPUTE)
            return vk::ShaderStageFlagBits::eCompute;
        if (stage == PE_SHADER_STAGE_RAYGEN_KHR)
            return vk::ShaderStageFlagBits::eRaygenKHR;
        if (stage == PE_SHADER_STAGE_ANY_HIT_KHR)
            return vk::ShaderStageFlagBits::eAnyHitKHR;
        if (stage == PE_SHADER_STAGE_CLOSEST_HIT_KHR)
            return vk::ShaderStageFlagBits::eClosestHitKHR;
        if (stage == PE_SHADER_STAGE_MISS_KHR)
            return vk::ShaderStageFlagBits::eMissKHR;
        if (stage == PE_SHADER_STAGE_INTERSECTION_KHR)
            return vk::ShaderStageFlagBits::eIntersectionKHR;
        if (stage == PE_SHADER_STAGE_CALLABLE_KHR)
            return vk::ShaderStageFlagBits::eCallableKHR;
        if (stage == PE_SHADER_STAGE_TASK_EXT)
            return vk::ShaderStageFlagBits::eTaskEXT;
        if (stage == PE_SHADER_STAGE_MESH_EXT)
            return vk::ShaderStageFlagBits::eMeshEXT;
        PE_ERROR("[Shader] Unsupported PeShaderStageFlags for single-stage conversion");
        return vk::ShaderStageFlagBits::eVertex;
    }

    VulkanShaderImpl::VulkanShaderImpl(Shader *owner, const ShaderDesc &desc)
        : m_owner{owner}
    {
        if (desc.type == ShaderCodeType::HLSL)
        {
            PE_ERROR_IF(!CompileHlsl(owner, desc.stage, Shader::m_globalDefines, desc.defines, m_spirv),
                        std::string("Failed to compile shader: " + desc.sourcePath).c_str());
        }
        else
        {
            PE_ERROR_IF(!CompileGlsl(owner, desc.stage, Shader::m_globalDefines, desc.defines, m_spirv),
                        std::string("Failed to compile shader: " + desc.sourcePath).c_str());
        }

        m_owner->m_cache.WriteSpvToFile(m_spirv);
    }

    VulkanShaderImpl::VulkanShaderImpl(Shader *owner, const uint32_t *spirv, size_t sizeWords)
        : m_owner{owner}
    {
        m_spirv.resize(sizeWords);
        memcpy(m_spirv.data(), spirv, sizeWords * sizeof(uint32_t));
    }

    VulkanShaderImpl::~VulkanShaderImpl()
    {
        DestroyModule();
    }

    vk::ShaderModule VulkanShaderImpl::GetOrCreateModule()
    {
        if (m_module)
            return m_module;

        if (m_spirv.empty())
            return vk::ShaderModule{};

        vk::ShaderModuleCreateInfo info{};
        info.codeSize = m_spirv.size() * sizeof(uint32_t);
        info.pCode = m_spirv.data();
        m_module = VulkanRhi::Device().createShaderModule(info);
        return m_module;
    }

    void VulkanShaderImpl::DestroyModule()
    {
        if (m_module)
        {
            VulkanRhi::Device().destroyShaderModule(m_module);
            m_module = vk::ShaderModule{};
        }
    }

} // namespace pe
