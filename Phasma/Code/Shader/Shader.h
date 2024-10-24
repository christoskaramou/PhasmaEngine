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

    class Shader : public PeHandle<Shader, ShaderApiHandle>
    {
    public:
        static void SetGlobalDefine(const std::string &name, const std::string &value);
        static std::vector<Descriptor *> ReflectPassDescriptors(const PassInfo &passInfo);

        Shader(const std::string &sourcePath, ShaderStage shaderStage, const std::vector<Define> &defines = {});
        ~Shader();

        const std::string &GetEntryName();
        ShaderStage GetShaderStage() { return m_shaderStage; }
        const uint32_t *GetSpriv() { return m_spirv.data(); }
        size_t Size() { return m_spirv.size(); }
        size_t BytesCount() { return m_spirv.size() * sizeof(uint32_t); }
        Reflection &GetReflection() { return m_reflection; }
        ShaderCache &GetCache() { return m_cache; }
        StringHash GetPathID() { return m_pathID; }
        const PushConstantDesc &GetPushConstantDesc() { return m_reflection.GetPushConstantDesc(); }
        size_t GetID() { return m_id; }

    private:
        bool CompileGlsl(const std::string &sourcePath, const std::vector<Define> &defines);
        void AddDefineGlsl(const Define &define, shaderc::CompileOptions &options);
        bool CompileHlsl(const std::string &sourcePath, const std::vector<Define> &defines);

        const size_t m_id;
        ShaderCache m_cache;
        Reflection m_reflection;
        ShaderStage m_shaderStage;
        shaderc::Compiler m_compiler;
        std::vector<uint32_t> m_spirv{};
        StringHash m_pathID;
        bool m_isHlsl = false;
        
        inline static std::vector<Define> m_globalDefines{};
    };
}
