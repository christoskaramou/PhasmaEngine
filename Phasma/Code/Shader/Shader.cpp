/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "Shader/Shader.h"
#include "Systems/EventSystem.h"

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

    Shader::Shader(const ShaderInfo& info)
    {
        std::string path = info.sourcePath;
        if (path.find(Path::Assets) == std::string::npos)
            path = Path::Assets + info.sourcePath;

        m_shaderType = info.shaderType;

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
            shaderc::CompileOptions options;
            options.SetIncluder(std::make_unique<FileIncluder>());
            options.SetOptimizationLevel(shaderc_optimization_level_performance);
            // m_options.SetHlslFunctionality1(true);

            AddDefines(m_globalDefines, options);
            AddDefines(info.defines, options);

            CompileFileToAssembly(static_cast<shaderc_shader_kind>(info.shaderType), options);
            CompileAssembly(static_cast<shaderc_shader_kind>(info.shaderType), options);
            
            m_cache.WriteSpvToFile(m_spirv);
        }
        else
        {
            m_spirv = m_cache.ReadSpvFromFile();
        }

        if (m_spirv.size() > 0)
            m_reflection.Init(this);

        // Watch the file for changes
        if (FileWatcher::Get(info.sourcePath) == nullptr)
        {
            auto modifiedCallback = []()
            { Context::Get()->GetSystem<EventSystem>()->PushEvent(EventType::CompileShaders); };
            FileWatcher::Add(path, modifiedCallback);
        }
    }

    Shader::~Shader()
    {
    }

    void Shader::AddDefine(Define &def, shaderc::CompileOptions &options)
    {
        if (def.name.empty())
            return;

        if (def.value.empty())
            options.AddMacroDefinition(def.name);
        else
            options.AddMacroDefinition(def.name, def.value);

        defines.push_back(def);
    }

    void Shader::AddDefines(const std::vector<Define> &defs, shaderc::CompileOptions &options)
    {
        for (auto def : defs)
            AddDefine(def, options);
    }

    std::string Shader::PreprocessShader(shaderc_shader_kind kind, shaderc::CompileOptions &options)
    {
        if (m_cache.GetShaderCode().empty() || m_cache.GetSourcePath().empty())
            PE_ERROR("source file was empty");

        shaderc::PreprocessedSourceCompilationResult result = m_compiler.PreprocessGlsl(
            m_cache.GetShaderCode(), kind, m_cache.GetSourcePath().c_str(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            std::cerr << result.GetErrorMessage();
            return "";
        }

        return {result.cbegin(), result.cend()};
    }

    void Shader::CompileFileToAssembly(shaderc_shader_kind kind, shaderc::CompileOptions &options)
    {
        if (m_cache.GetShaderCode().empty() || m_cache.GetSourcePath().empty())
            PE_ERROR("source file was empty");

        shaderc::AssemblyCompilationResult result = m_compiler.CompileGlslToSpvAssembly(
            m_cache.GetShaderCode(), kind, m_cache.GetSourcePath().c_str(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            std::cerr << result.GetErrorMessage();
            m_cache.SetAssembly("");
            return;
        }

        m_cache.SetAssembly({result.cbegin(), result.cend()});
    }

    void Shader::CompileAssembly(shaderc_shader_kind kind, shaderc::CompileOptions &options)
    {
        if (m_cache.GetAssembly().empty())
            PE_ERROR("assembly was empty");

        shaderc::SpvCompilationResult result = m_compiler.AssembleToSpv(m_cache.GetAssembly(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            std::cerr << result.GetErrorMessage();
            m_spirv = {};
            return;
        }

        m_spirv = {result.cbegin(), result.cend()};
    }

    void Shader::CompileFile(shaderc_shader_kind kind, shaderc::CompileOptions &options)
    {
        if (m_cache.GetShaderCode().empty() || m_cache.GetSourcePath().empty())
            PE_ERROR("source file was empty");

        shaderc::SpvCompilationResult module = m_compiler.CompileGlslToSpv(
            m_cache.GetShaderCode(), kind, m_cache.GetSourcePath().c_str(), options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            std::cerr << module.GetErrorMessage();
            m_spirv = {};
        }

        m_spirv = {module.cbegin(), module.cend()};
    }
}
