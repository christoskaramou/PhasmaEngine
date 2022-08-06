#include "Shader/Shader.h"

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

    Shader::Shader(const ShaderInfo &info)
    {
        std::string path = info.sourcePath;
        if (path.find(Path::Assets) == std::string::npos)
            path = Path::Assets + info.sourcePath;

        m_isHlsl = path.ends_with(".hlsl");

        m_pathID = StringHash(path);

        m_shaderStage = info.shaderStage;

        Hash definesHash;
        for (const Define &def : m_globalDefines)
        {
            definesHash.Combine(def.name);
            definesHash.Combine(def.value);
        }
        for (const Define &def : info.defines)
        {
            definesHash.Combine(def.name);
            definesHash.Combine(def.value);
        }

        m_cache.Init(path, definesHash);

        if (m_cache.ShaderNeedsCompile())
        {
            if (m_isHlsl)
            {
                CompileHlsl(info, m_cache);
            }
            else
            {
                shaderc::CompileOptions options;

                options.SetIncluder(std::make_unique<FileIncluder>());
                options.SetOptimizationLevel(shaderc_optimization_level_performance);

                for (auto def : m_globalDefines)
                    AddDefineGlsl(def, options);

                for (auto def : info.defines)
                    AddDefineGlsl(def, options);

#if PE_DEBUG_MODE
                // Useful for debugging shaders
                options.SetGenerateDebugInfo();
#endif

                uint32_t shaderFlags = 0;
                if (m_shaderStage == ShaderStage::VertexBit)
                    shaderFlags |= shaderc_shader_kind::shaderc_vertex_shader;
                else if (m_shaderStage == ShaderStage::FragmentBit)
                    shaderFlags |= shaderc_shader_kind::shaderc_fragment_shader;
                else if (m_shaderStage == ShaderStage::ComputeBit)
                    shaderFlags |= shaderc_shader_kind::shaderc_compute_shader;
                else
                    PE_ERROR("Invalid shader stage!");

                CompileGlsl(static_cast<shaderc_shader_kind>(shaderFlags), options);
            }

            m_cache.WriteSpvToFile(m_spirv);
        }
        else
        {
            m_spirv = m_cache.ReadSpvFile();
        }

        if (m_spirv.size() > 0)
            m_reflection.Init(this);
    }

    Shader::~Shader()
    {
    }

    std::string &Shader::GetEntryName()
    {
        static std::string empty = "";
        static std::string entryNameGlsl = "main";
        static std::string entryNameHlslVS = "mainVS";
        static std::string entryNameHlslPS = "mainPS";
        static std::string entryNameHlslCS = "mainCS";

        if (m_isHlsl)
        {
            if (m_shaderStage == ShaderStage::VertexBit)
                return entryNameHlslVS;
            if (m_shaderStage == ShaderStage::FragmentBit)
                return entryNameHlslPS;
            if (m_shaderStage == ShaderStage::ComputeBit)
                return entryNameHlslCS;

            PE_ERROR("Invalid shader stage!");
        }
        else
        {
            return entryNameGlsl;
        }

        PE_ERROR("Invalid shader stage!");
        return empty;
    }

    void Shader::AddDefineGlsl(Define &def, shaderc::CompileOptions &options)
    {
        if (def.name.empty())
            return;

        if (def.value.empty())
            options.AddMacroDefinition(def.name);
        else
            options.AddMacroDefinition(def.name, def.value);

        defines.push_back(def);
    }

    bool Shader::CompileGlsl(shaderc_shader_kind kind, shaderc::CompileOptions &options)
    {
        if (m_cache.GetShaderCode().empty() || m_cache.GetSourcePath().empty())
            PE_ERROR("source file was empty");

        shaderc::SpvCompilationResult module = m_compiler.CompileGlslToSpv(
            m_cache.GetShaderCode(), kind, m_cache.GetSourcePath().c_str(), GetEntryName().c_str(), options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            std::cerr << module.GetErrorMessage();
            return false;
        }

        m_spirv = {module.cbegin(), module.cend()};

        return true;
    }

    bool Shader::CompileHlsl(const ShaderInfo &info, ShaderCache &cache)
    {
        std::string path = info.sourcePath;
        if (path.find(Path::Assets) == std::string::npos)
            path = Path::Assets + info.sourcePath;

        std::vector<LPCWSTR> args{};

        args.push_back(L"-spirv");

        // Shade model
        if (info.shaderStage == ShaderStage::VertexBit)
        {
            // Shader model
            args.push_back(L"-T");
            args.push_back(L"vs_6_0");
        }
        else if (info.shaderStage == ShaderStage::FragmentBit)
        {
            // Shader model
            args.push_back(L"-T");
            args.push_back(L"ps_6_0");
        }
        else if (info.shaderStage == ShaderStage::ComputeBit)
        {
            // Shader model
            args.push_back(L"-T");
            args.push_back(L"cs_6_0");
        }
        else
            PE_ERROR("Invalid shader stage!");

        // Entry point
        std::string entryName = GetEntryName();
        std::wstring entryNameW(entryName.begin(), entryName.end());
        args.push_back(L"-E");
        args.push_back(entryNameW.c_str());

        args.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);   //-WX
        args.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR); //-Zp

#if PE_DEBUG_MODE
        // Generate symbols
        args.push_back(DXC_ARG_DEBUG); //-Zi
#endif

        // Defines
        for (const Define &def : info.defines)
        {
            std::string define = def.name;
            if (!def.value.empty())
                define += "=" + def.value;

            args.push_back(L"-D");
            args.push_back(std::wstring(define.begin(), define.end()).c_str());
        }
        using namespace Microsoft::WRL;
        ComPtr<IDxcUtils> dxc_utils;
        auto hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxc_utils.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            return false;

        ComPtr<IDxcIncludeHandler> include_handler;
        hr = dxc_utils->CreateDefaultIncludeHandler(include_handler.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        ComPtr<IDxcCompiler3> dxc_compiler;
        hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc_compiler));
        if (FAILED(hr))
            return false;

        ComPtr<IDxcUtils> pUtils;
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf()));
        ComPtr<IDxcBlobEncoding> pSource;
        uint32_t size = static_cast<uint32_t>(cache.GetShaderCode().size());
        pUtils->CreateBlob(cache.GetShaderCode().c_str(), size, CP_UTF8, pSource.GetAddressOf());

        DxcBuffer sourceBuffer;
        sourceBuffer.Ptr = pSource->GetBufferPointer();
        sourceBuffer.Size = pSource->GetBufferSize();
        sourceBuffer.Encoding = 0;

        ComPtr<IDxcResult> result;
        hr = dxc_compiler->Compile(
            &sourceBuffer,
            args.data(),
            static_cast<uint32_t>(args.size()),
            include_handler.Get(),
            IID_PPV_ARGS(&result));

        // Error Handling
        ComPtr<IDxcBlobUtf8> pErrors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
        if (pErrors && pErrors->GetStringLength() > 0)
            PE_ERROR(pErrors->GetStringPointer());

        // Get shader output
        ComPtr<IDxcBlob> pObject;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(pObject.GetAddressOf()), nullptr);
        if (!pObject)
            return false;

        m_spirv.resize(pObject->GetBufferSize() / sizeof(uint32_t));
        memcpy(m_spirv.data(), pObject->GetBufferPointer(), pObject->GetBufferSize());

        return true;
    }
}
