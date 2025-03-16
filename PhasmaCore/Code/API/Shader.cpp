#include "API/Shader.h"
#include "API/Pipeline.h"

namespace pe
{
    shaderc_include_result *MakeErrorIncludeResult(const char *message)
    {
        return new shaderc_include_result{"", 0, message, strlen(message)};
    }

    shaderc_include_result *
    FileIncluder::GetInclude(const char *requested_source, shaderc_include_type, const char *requesting_source,
                             size_t)
    {
        std::filesystem::path requesting_source_path(requesting_source);

        std::string full_path;
        if (requesting_source_path.is_relative())
        {
            full_path =
                std::filesystem::current_path().string() + "\\" +
                requesting_source_path.parent_path().string() + "\\" +
                requested_source;
        }
        else
            full_path = requesting_source_path.parent_path().string() + "\\" + requested_source;

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

    // Function to validate a single shader stage
    void ValidateShaderStage(Shader *shader, ShaderStage expectedStage)
    {
        PE_ERROR_IF(shader->GetShaderStage() != expectedStage, "Invalid shader stage");
    }

    // Helper function to get a descriptor safely
    Descriptor *GetDescriptorSafely(const std::vector<Descriptor *> &descriptors, size_t index)
    {
        return (index < descriptors.size()) ? descriptors[index] : nullptr;
    }

    // Function to combine descriptor sets from vertex and fragment shaders
    std::vector<Descriptor *> CombineDescriptors(const std::vector<Descriptor *> &vertDesc, const std::vector<Descriptor *> &fragDesc)
    {
        std::vector<Descriptor *> descriptors;
        size_t maxSize = std::max(vertDesc.size(), fragDesc.size());
        descriptors.reserve(maxSize);

        for (size_t i = 0; i < maxSize; ++i)
        {
            // one of the two must have a valid descriptor (!= nullptr)
            // nullptr means that the descriptor is not present in one of the shaders
            // if both are nullptr, it means that this Set index (i) is not used in both shaders and this is not allowed
            Descriptor *vertDescriptor = GetDescriptorSafely(vertDesc, i);
            Descriptor *fragDescriptor = GetDescriptorSafely(fragDesc, i);
            PE_ERROR_IF(!vertDescriptor && !fragDescriptor, "Descriptor set is not used by any shader");
            descriptors.push_back(vertDescriptor ? vertDescriptor : fragDescriptor);
        }

        return descriptors;
    }

    std::vector<Descriptor *> Shader::ReflectPassDescriptors(const PassInfo &passInfo)
    {
        Shader *comp = passInfo.pCompShader;
        Shader *vert = passInfo.pVertShader;
        Shader *frag = passInfo.pFragShader;

        if (vert && frag)
        {
            ValidateShaderStage(vert, ShaderStage::VertexBit);
            ValidateShaderStage(frag, ShaderStage::FragmentBit);
            auto vertDesc = vert->GetReflection().GetDescriptors();
            auto fragDesc = frag->GetReflection().GetDescriptors();
            return CombineDescriptors(vertDesc, fragDesc);
        }
        else if (vert)
        {
            ValidateShaderStage(vert, ShaderStage::VertexBit);
            return vert->GetReflection().GetDescriptors();
        }
        else if (frag)
        {
            ValidateShaderStage(frag, ShaderStage::FragmentBit);
            return frag->GetReflection().GetDescriptors();
        }
        else if (comp)
        {
            ValidateShaderStage(comp, ShaderStage::ComputeBit);
            return comp->GetReflection().GetDescriptors();
        }
        else
        {
            PE_ERROR("This state is not used in the current implementation");
            return {};
        }
    }

    Shader::Shader(const uint32_t *spirv, size_t size, ShaderStage shaderStage, const std::string &entryName)
        : m_shaderStage{shaderStage},
          m_entryName{entryName}
    {
        m_spirv.resize(size);
        memcpy(m_spirv.data(), spirv, size * sizeof(uint32_t));

        if (m_spirv.size() > 0)
        {
            m_reflection.Init(this);
        }
    }

    Shader::Shader(const std::string &spirvPath, ShaderStage shaderStage, const std::string &entryName)
        : m_shaderStage{shaderStage},
          m_entryName{entryName}
    {
        std::string path = spirvPath;
        std::replace(path.begin(), path.end(), '\\', '/');
        m_pathID = StringHash(path);

        // Read spirv file
        {
            FileSystem file(path, std::ios::in | std::ios::ate | std::ios::binary);
            PE_ERROR_IF(!file.IsOpen(), "failed to open file!");

            std::string buffer = file.ReadAll();
            m_spirv.resize(file.Size() / sizeof(uint32_t));
            memcpy(m_spirv.data(), buffer.data(), file.Size());
        }

        if (m_spirv.size() > 0)
        {
            m_reflection.Init(this);
        }
    }

    Shader::Shader(const std::string &sourcePath, ShaderStage shaderStage, const std::string &entryName, const std::vector<Define> &localDefines, ShaderCodeType type)
        : m_localDefines{localDefines},
          m_shaderStage{shaderStage},
          m_type{type},
          m_entryName{entryName}
    {
        std::string path = sourcePath;
        std::replace(path.begin(), path.end(), '\\', '/');
        PE_ERROR_IF(!FileWatcher::Get(path), "Shader file does not exist");
        m_pathID = StringHash(path);

        Hash definesHash;
        for (const Define &def : m_globalDefines)
        {
            definesHash.Combine(def.name);
            definesHash.Combine(def.value);
        }
        for (const Define &def : localDefines)
        {
            definesHash.Combine(def.name);
            definesHash.Combine(def.value);
        }

        m_cache.Init(path, definesHash);

        if (m_cache.ShaderNeedsCompile())
        {
            if (m_type == ShaderCodeType::HLSL)
            {
                PE_ERROR_IF(!CompileHlsl(localDefines), "Failed to compile shader:" + path);
            }
            else
            {
                PE_ERROR_IF(!CompileGlsl(localDefines), "Failed to compile shader: " + path);
            }

            m_cache.WriteSpvToFile(m_spirv);
        }
        else
        {
            m_spirv = m_cache.ReadSpvFile();
        }

        if (m_spirv.size() > 0)
        {
            m_reflection.Init(this);
        }
    }

    Shader::~Shader()
    {
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

        Define define{name, value};
        m_globalDefines.push_back(define);
    }

    void Shader::AddDefineGlsl(const Define &def, shaderc::CompileOptions &options)
    {
        if (def.name.empty())
            return;

        if (def.value.empty())
            options.AddMacroDefinition(def.name);
        else
            options.AddMacroDefinition(def.name, def.value);
    }

    bool Shader::CompileGlsl(const std::vector<Define> &localDefines)
    {
        shaderc::CompileOptions options;

        options.SetIncluder(std::make_unique<FileIncluder>());
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        for (const Define &def : m_globalDefines)
        {
            AddDefineGlsl(def, options);
        }

        for (const Define &def : localDefines)
        {
            AddDefineGlsl(def, options);
        }

#if PE_DEBUG_MODE
        // Useful for debugging shaders
        options.SetGenerateDebugInfo();
#endif

        uint32_t shaderKindFlags = 0;
        if (m_shaderStage == ShaderStage::VertexBit)
        {
            shaderKindFlags |= shaderc_shader_kind::shaderc_vertex_shader;
        }
        else if (m_shaderStage == ShaderStage::FragmentBit)
        {
            shaderKindFlags |= shaderc_shader_kind::shaderc_fragment_shader;
        }
        else if (m_shaderStage == ShaderStage::ComputeBit)
        {
            shaderKindFlags |= shaderc_shader_kind::shaderc_compute_shader;
        }
        else
        {
            PE_ERROR("Invalid shader stage!");
        }

        shaderc::SpvCompilationResult module = m_compiler.CompileGlslToSpv(
            m_cache.GetShaderCode(), static_cast<shaderc_shader_kind>(shaderKindFlags), m_cache.GetSourcePath().c_str(), GetEntryName().c_str(), options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            std::cerr << module.GetErrorMessage();
            return false;
        }

        m_spirv = {module.cbegin(), module.cend()};

        return true;
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

    namespace
    {
        void PrintDxcError(IDxcResult *result)
        {
            // Error Handling
            IDxcBlobUtf8 *pErrors = nullptr;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
            if (pErrors && pErrors->GetStringLength() > 0)
                PE_INFO(pErrors->GetStringPointer());

            pErrors->Release();
        }
    }

    bool Shader::CompileHlsl(const std::vector<Define> &localDefines)
    {
        std::vector<LPCWSTR> args{};

        args.push_back(L"-spirv");

        // Shader stage
        args.push_back(L"-T");
        if (m_shaderStage == ShaderStage::VertexBit)
        {
            args.push_back(L"vs_6_0");
        }
        else if (m_shaderStage == ShaderStage::FragmentBit)
        {
            args.push_back(L"ps_6_0");
        }
        else if (m_shaderStage == ShaderStage::ComputeBit)
        {
            args.push_back(L"cs_6_0");
        }
        else
        {
            PE_ERROR("Invalid shader stage!");
        }
        // args.push_back(L"-fspv-preserve-bindings");
        args.push_back(L"-fspv-preserve-interface");
        // args.push_back(L"-fspv-reflect");

        // Entry point
        args.push_back(L"-E");
        std::wstring entryName = ConvertUtf8ToWide(GetEntryName());
        args.push_back(entryName.c_str());

        args.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);   //-WX
        args.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR); //-Zpr
                                                       // args.push_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR); //-Zpc

#if PE_DEBUG_MODE
        // Generate symbols
        args.push_back(DXC_ARG_DEBUG); //-Zi
#endif

        // Defines
        std::vector<std::wstring> wdefines(m_globalDefines.size() + localDefines.size());
        uint32_t i = 0;

        for (const Define &def : m_globalDefines)
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
        auto hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils));
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
        uint32_t size = static_cast<uint32_t>(m_cache.GetShaderCode().size());
        pUtils->CreateBlob(m_cache.GetShaderCode().c_str(), size, CP_UTF8, &pSource);

        DxcBuffer sourceBuffer;
        sourceBuffer.Ptr = pSource->GetBufferPointer();
        sourceBuffer.Size = pSource->GetBufferSize();
        sourceBuffer.Encoding = 0;

        // TODO: Exception thrown at 0x00007FFFCDDEFA4C in Phasma.exe: Microsoft C++ exception: hlsl::Exception at memory location 0x0000000EC72FB3B0.
        IDxcResult *result = nullptr;
        hr = dxc_compiler->Compile(
            &sourceBuffer,
            args.data(),
            static_cast<uint32_t>(args.size()),
            include_handler,
            IID_PPV_ARGS(&result));

        HRESULT compileStatus;
        result->GetStatus(&compileStatus);

        if (FAILED(compileStatus))
        {
            PrintDxcError(result);
            return false;
        }

        // Get shader output
        IDxcBlob *pObject = nullptr;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pObject), nullptr);
        if (!pObject)
        {
            PrintDxcError(result);
            return false;
        }

        m_spirv.resize(pObject->GetBufferSize() / sizeof(uint32_t));
        memcpy(m_spirv.data(), pObject->GetBufferPointer(), pObject->GetBufferSize());

        dxc_utils->Release();
        include_handler->Release();
        dxc_compiler->Release();
        pUtils->Release();
        pSource->Release();
        result->Release();
        pObject->Release();

        return true;
    }
}
