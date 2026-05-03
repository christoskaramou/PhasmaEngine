#pragma once

namespace pe
{
    class ShaderCache
    {
    public:
        // Init() resolves #include directives in `sourcePath` and keys the on-disk cache on a hash of
        // the resolved-source content combined with `entryPoint` and `definesHash`. The key is therefore
        // backend-agnostic — same HLSL source produces the same key whether compiled to SPIR-V or DXIL.
        // Callers are responsible for folding any other compile-time inputs that affect output bytecode
        // (such as shader stage) into `definesHash` before calling Init — `Shader::Create` does so for stage.
        void Init(const std::string &sourcePath, const std::string &entryPoint, size_t definesHash = 0);
        bool ShaderNeedsCompile();
        inline const std::string &GetSourcePath() { return m_sourcePath; }
        inline const std::string &GetShaderCode() { return m_code; }
        size_t GetHash() { return m_hash; }
        std::vector<uint8_t> ReadBytecodeFile();
        void WriteBytecodeToFile(const std::vector<uint8_t> &bytecode);
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
