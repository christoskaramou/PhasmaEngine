#pragma once

namespace pe
{
    class ShaderCache
    {
    public:
        // Init() resolves #include directives in `sourcePath` and keys the on-disk cache on a hash of
        // the resolved-source content combined with `entryPoint` and `definesHash`. The key is therefore
        // backend-agnostic — same HLSL source produces the same key whether compiled to SPIR-V or DXIL.
        // NOTE: shader stage is NOT part of the key. Two shaders with identical source + entry + defines
        // but different stages would collide; relying on convention (entry name differs per stage) for now.
        void Init(const std::string &sourcePath, const std::string &entryPoint, size_t definesHash = 0);
        bool ShaderNeedsCompile();
        inline const std::string &GetSourcePath() { return m_sourcePath; }
        inline const std::string &GetShaderCode() { return m_code; }
        size_t GetHash() { return m_hash; }
        std::vector<uint32_t> ReadSpvFile();
        std::string ParseShader(const std::string &sourcePath);
        void WriteSpvToFile(const std::vector<uint32_t> &spirv);

    private:
        std::string m_sourcePath;
        std::string m_code;
        StringHash m_hash;
        std::string m_tempFilePath;
    };
} // namespace pe
