#pragma once

#include "API/Reflection.h"
#include "API/ShaderCache.h"
#include "API/Descriptor.h"
#include "shaderc/shaderc.hpp"
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
        static void AddGlobalDefine(const std::string &name, const std::string &value);
        static std::vector<Descriptor *> ReflectPassDescriptors(const PassInfo &passInfo);

        Shader(const uint32_t *spirv, size_t size, ShaderStage shaderStage, const std::string &entryName);
        Shader(const std::string &spirvPath, ShaderStage shaderStage, const std::string &entryName);
        Shader(const std::string &sourcePath, ShaderStage shaderStage, const std::string &entryName, const std::vector<Define> &localDefines, ShaderCodeType type = ShaderCodeType::HLSL);
        ~Shader();

        const std::string &GetEntryName() { return m_entryName; }
        ShaderStage GetShaderStage() { return m_shaderStage; }
        const uint32_t *GetSpriv() { return m_spirv.data(); }
        size_t Size() { return m_spirv.size(); }
        size_t BytesCount() { return m_spirv.size() * sizeof(uint32_t); }
        Reflection &GetReflection() { return m_reflection; }
        ShaderCache &GetCache() { return m_cache; }
        size_t GetPathID() { return m_pathID; }
        const PushConstantDesc &GetPushConstantDesc() { return m_reflection.GetPushConstantDesc(); }
        std::vector<Define> &GetLocalDefines() { return m_localDefines; }

    private:
        bool CompileGlsl(const std::vector<Define> &localDefines);
        void AddDefineGlsl(const Define &define, shaderc::CompileOptions &options);
        bool CompileHlsl(const std::vector<Define> &localDefines);

        std::vector<Define> m_localDefines;
        ShaderCache m_cache;
        Reflection m_reflection;
        ShaderStage m_shaderStage;
        shaderc::Compiler m_compiler;
        std::vector<uint32_t> m_spirv{};
        size_t m_pathID;
        ShaderCodeType m_type;
        std::string m_entryName;
        
        inline static std::vector<Define> m_globalDefines{};
    };
}
