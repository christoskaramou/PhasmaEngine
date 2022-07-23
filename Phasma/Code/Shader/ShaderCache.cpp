#include "Shader/ShaderCache.h"

namespace pe
{
    // Parse includes recursively and return the full code
    std::string ShaderCache::ParseFileAndIncludes(const std::string &sourcePath)
    {
        if (!std::filesystem::exists(sourcePath))
            PE_ERROR("file does not exist!");

        std::filesystem::path path(sourcePath);
        FileSystem file(path.string(), std::ios::in | std::ios::ate);
        if (!file.IsOpen())
            PE_ERROR("file could not be opened!");

        std::string code = file.ReadAll();
        file.SetReadCursor(0);
        std::string line;
        while (line = file.ReadLine(), !file.EndOfFile())
        {
            size_t commentPos = line.find("//");
            if (commentPos != std::string::npos)
                line = line.substr(0, commentPos);

            size_t pos = line.find("#include");
            if (pos != std::string::npos)
            {
                std::string includeFile = line.substr(line.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                std::string includeFileFull = path.remove_filename().string() + includeFile;

                if (std::find(m_includes.begin(), m_includes.end(), includeFileFull) == m_includes.end())
                    m_includes.push_back(includeFileFull);

                size_t posStart = code.find(line);
                if (posStart != std::string::npos)
                {
                    size_t posEnd = code.find("\n", posStart);
                    code.erase(posStart, posEnd - posStart);
                    code.insert(posStart, ParseFileAndIncludes(includeFileFull));
                }
            }
        }

        return code;
    }

    void ShaderCache::Init(const std::string &sourcePath, const Hash &definesHash)
    {
        m_includes = {};

        m_sourcePath = sourcePath;
        m_code = ParseFileAndIncludes(m_sourcePath);

        m_hash = StringHash(m_code);
        m_hash.Combine(definesHash);

        m_tempFilePath = std::regex_replace(
            std::filesystem::path(m_sourcePath).parent_path().string(),
            std::regex("Shaders"), "ShaderCache");
        m_tempFilePath += "/" + std::to_string(m_hash);
    }

    bool ShaderCache::ShaderNeedsCompile()
    {
        return !std::filesystem::exists(m_tempFilePath);
    }

    std::vector<uint32_t> ShaderCache::ReadSpvFile()
    {
        FileSystem file(m_tempFilePath, std::ios::in | std::ios::ate | std::ios::binary);
        if (!file.IsOpen())
            PE_ERROR("failed to open file!");

        std::string buffer = file.ReadAll();

        std::vector<uint32_t> spirv;
        spirv.resize(file.Size() / sizeof(uint32_t));
        memcpy(spirv.data(), buffer.data(), file.Size());

        return spirv;
    }

    void ShaderCache::WriteSpvToFile(const std::vector<uint32_t> &spirv)
    {
        std::filesystem::path path(m_tempFilePath);
        if (!std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());

        FileSystem file(m_tempFilePath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.IsOpen())
            PE_ERROR("failed to open file!");

        file.Write(reinterpret_cast<const char *>(spirv.data()), spirv.size() * sizeof(uint32_t));
    }
}