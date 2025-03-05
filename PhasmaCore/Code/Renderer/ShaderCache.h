#pragma once

namespace pe
{
    class ShaderCache
    {
    public:
        void Init(const std::string &sourcePath, size_t definesHash = 0);
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
        std::vector<std::string> m_includes;
    };
}
