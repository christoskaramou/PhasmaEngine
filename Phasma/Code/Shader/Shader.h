#pragma once

#include "shaderc/shaderc.hpp"
#include "Shader/Reflection.h"
#include "Shader/ShaderCache.h"
#include "dxc/dxcapi.h"
#include "Renderer/Descriptor.h"

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

    class Reflection;
    class PassInfo;

    class Shader : public IHandle<Shader, ShaderHandle>
    {
    public:
        Shader(const std::string &sourcePath, ShaderStage shaderStage, const std::vector<Define> &defines = {});

        ~Shader();

        std::string &GetEntryName();

        ShaderStage GetShaderStage() { return m_shaderStage; }

        const uint32_t *GetSpriv() { return m_spirv.data(); }

        size_t Size() { return m_spirv.size(); }

        size_t BytesCount() { return m_spirv.size() * sizeof(uint32_t); }

        Reflection &GetReflection() { return m_reflection; }

        static void SetGlobalDefine(const std::string &name, const std::string &value)
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

        static std::vector<Descriptor *> PassDescriptors(const PassInfo &passInfo);

        ShaderCache &GetCache() { return m_cache; }

        StringHash GetPathID() { return m_pathID; }

        const PushConstantDesc &GetPushConstantDesc() { return m_reflection.GetPushConstantDesc(); }

    private:
        bool CompileGlsl(shaderc_shader_kind kind, shaderc::CompileOptions &options);

        void AddDefineGlsl(const Define &define, shaderc::CompileOptions &options);

        bool CompileHlsl(const std::string &sourcePath, ShaderStage shaderStage, const std::vector<Define> &defines, const std::string &shaderCode);

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