#pragma once

#include "shaderc/shaderc.hpp"
#include "Shader/Reflection.h"
#include "Shader/ShaderCache.h"
#include "dxc/dxcapi.h"

namespace pe
{
    struct Define
    {
        std::string name{};
        std::string value{};
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
        ShaderStage shaderStage;
        std::vector<Define> defines{};
    };

    class Reflection;

    class Shader : public IHandle<Shader, ShaderHandle>
    {
    public:
        Shader(const ShaderInfo &info);

        ~Shader();

        std::string &GetEntryName();

        inline ShaderStage GetShaderStage() { return m_shaderStage; }

        inline const uint32_t *GetSpriv() { return m_spirv.data(); }

        inline size_t Size() { return m_spirv.size(); }

        inline size_t BytesCount() { return m_spirv.size() * sizeof(uint32_t); }

        inline Reflection &GetReflection() { return m_reflection; }

        inline static void SetGlobalDefine(const std::string &name, const std::string &value)
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

        inline ShaderCache &GetCache() { return m_cache; }

        inline StringHash GetPathID() { return m_pathID; }

    private:
        bool CompileGlsl(shaderc_shader_kind kind, shaderc::CompileOptions &options);

        void AddDefineGlsl(Define &define, shaderc::CompileOptions &options);

        bool CompileHlsl(const ShaderInfo &info, ShaderCache &cache);

        ShaderCache m_cache;
        Reflection m_reflection;
        ShaderStage m_shaderStage;
        shaderc::Compiler m_compiler;
        std::vector<Define> defines{};
        std::vector<uint32_t> m_spirv{};
        StringHash m_pathID;

        bool m_isHlsl = false;

        inline static std::vector<Define> m_globalDefines{};
    };
}