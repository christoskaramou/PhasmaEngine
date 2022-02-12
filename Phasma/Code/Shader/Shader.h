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

#pragma once

#include "shaderc/shaderc.hpp"
#include "Shader/Reflection.h"
#include "Shader/ShaderCache.h"

namespace pe
{
    struct Define
    {
        std::string name{};
        std::string value{};
    };

    enum class ShaderType
    {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        TessControl,
        TessEvaluation
    };

    class FileIncluder : public shaderc::CompileOptions::IncluderInterface
    {
    public:
        shaderc_include_result *GetInclude(const char *requested_source, shaderc_include_type, const char *requesting_source, size_t) override;

        void ReleaseInclude(shaderc_include_result *include_result) override;

        inline const std::unordered_set<std::string> &file_path_trace() const
        {
            return included_files;
        }

    private:
        class FileInfo
        {
        public:
            const std::string full_path;
            std::string contents;
        };
        std::unordered_set<std::string> included_files;
    };

    class ShaderInfo
    {
    public:
        std::string sourcePath;
        ShaderType shaderType;
        std::vector<Define> defines{};
    };

    class Reflection;

    class Shader : public IHandle<Shader, Placeholder>
    {
    public:
        Shader(const ShaderInfo &info);

        ~Shader();

        inline ShaderType GetShaderType() { return m_shaderType; }

        inline const uint32_t *GetSpriv() { return m_spirv.data(); }

        inline size_t Size() { return m_spirv.size(); }

        inline size_t BytesCount() { return m_spirv.size() * sizeof(uint32_t); }

        inline Reflection &GetReflection() { return m_reflection; }

        inline static void AddGlobalDefine(const std::string &name, const std::string &value)
        {
            for (auto &def : m_globalDefines)
            {
                if (def.name == name)
                    return;
            }

            Define define{name, value};
            m_globalDefines.push_back(define);
        }

        inline ShaderCache &GetCache() { return m_cache; }

    private:
        std::string PreprocessShader(shaderc_shader_kind kind, shaderc::CompileOptions &options);

        bool CompileFileToAssembly(shaderc_shader_kind kind, shaderc::CompileOptions &options);

        bool CompileAssembly(shaderc_shader_kind kind, shaderc::CompileOptions &options);

        bool CompileFile(shaderc_shader_kind kind, shaderc::CompileOptions &options);

        void AddDefine(Define &define, shaderc::CompileOptions &options);

        ShaderCache m_cache;
        Reflection m_reflection;
        ShaderType m_shaderType;
        shaderc::Compiler m_compiler;
        std::vector<Define> defines{};
        std::vector<uint32_t> m_spirv{};

        inline static std::vector<Define> m_globalDefines{};
    };
}