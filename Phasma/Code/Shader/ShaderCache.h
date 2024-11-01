#pragma once

namespace pe
{
    class ShaderCache
    {
    public:
        void Init(const std::string &sourcePath, size_t definesHash = 0);
        bool ShaderNeedsCompile();
        inline const std::string &GetShaderCode() { return m_code; }
        inline const std::string &GetSourcePath() { return m_sourcePath; }
        inline const std::string &GetAssembly() { return m_assembly; }
        inline void SetAssembly(const std::string &assembly) { m_assembly = assembly; }
        std::vector<uint32_t> ReadSpvFile();
        std::string ParseFileAndIncludes(const std::string &sourcePath);
        void WriteSpvToFile(const std::vector<uint32_t> &spirv);
        void WriteToTempFile();
        void WriteToTempAsm();
        size_t ReadHash();
        size_t GetHash() { return m_hash; }
        const std::vector<std::string> &GetIncludes() { return m_includes; }

    private:
        std::string m_sourcePath;
        std::string m_code;
        StringHash m_hash;
        std::string m_tempFilePath;
        std::string m_assembly;
        std::string m_preprocessed;
        std::vector<std::string> m_includes;
    };
}
