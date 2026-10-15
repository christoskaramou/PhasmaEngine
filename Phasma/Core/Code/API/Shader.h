#pragma once

#include "API/RHITypes.h"
#include "API/Reflection.h"
#include "API/ShaderCache.h"

namespace pe
{
    struct Define
    {
        std::string name{};
        std::string value{};
    };

    struct ShaderDesc
    {
        std::string sourcePath;
        std::string entryPoint = "main";
        PeShaderStageFlags stage = 0;
        std::vector<Define> defines;
        ShaderCodeType type = ShaderCodeType::HLSL;
        std::string debugName;
    };

    struct ShaderBytecodeDesc
    {
        const uint8_t *spirv = nullptr;
        size_t spirvSizeBytes = 0;
        const uint8_t *dxil = nullptr;
        size_t dxilSizeBytes = 0;
        PeShaderStageFlags stage = 0;
        std::string entryPoint = "main";
        std::string debugName;
        std::string reflectionSource;
    };

    // Compiles runtime HLSL: DXC (default) or slangc (PE_SLANGC_PATH). Chosen once at startup by ResolveShaderCompiler
    // (--shader-compiler, or "shader_compiler" in phasma_settings.json) and part of the shader-cache key.
    enum class ShaderCompiler : uint8_t
    {
        Dxc,
        Slang
    };

    class PassInfo;
    class Descriptor;

    class Shader : public NoCopy
    {
    public:
        struct Impl;

        static Shader *Create(const ShaderDesc &desc);
        static Shader *CreateFromBytecode(const ShaderBytecodeDesc &desc);
        static void Destroy(Shader *&shader);
        static std::vector<Shader *> GetHandles();

        static void SetCompiler(ShaderCompiler compiler);
        static ShaderCompiler GetCompiler();
        // .slang sources always compile with slangc (Slang syntax); everything else follows GetCompiler().
        static bool UsesSlang(const std::string &sourcePath);

        static void AddGlobalDefine(const std::string &name, const std::string &value);
        static size_t GetGlobalDefinesHash();
        static std::vector<Descriptor *> ReflectPassDescriptors(const PassInfo &passInfo);

        const std::string &GetEntryName() const { return m_entryName; }
        PeShaderStageFlags GetShaderStage() const { return m_stage; }
        Reflection &GetReflection() { return m_reflection; }
        const Reflection &GetReflection() const { return m_reflection; }
        ShaderCache &GetCache() { return m_cache; }
        size_t GetPathID() const { return m_pathID; }
        const PushConstantDesc &GetPushConstantDesc() const { return m_reflection.GetPushConstantDesc(); }
        std::vector<Define> &GetLocalDefines() { return m_localDefines; }
        const std::string &GetReflectionSource() const { return m_reflectionSource; }

    private:
        friend struct VulkanShaderImpl;
        friend struct Dx12ShaderImpl;

        Shader();
        ~Shader();

        Impl *m_impl{};
        std::vector<Define> m_localDefines{};
        ShaderCache m_cache{};
        Reflection m_reflection{};
        PeShaderStageFlags m_stage{};
        size_t m_pathID{};
        ShaderCodeType m_type{ShaderCodeType::HLSL};
        std::string m_entryName{};
        std::string m_reflectionSource{};

        inline static std::vector<Define> m_globalDefines{};
    };
} // namespace pe
