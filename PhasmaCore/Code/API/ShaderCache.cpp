#include "API/ShaderCache.h"
#include "API/RHI.h"

namespace pe
{
    // Parse includes recursively and return the full code
    std::string ShaderCache::ParseShader(const std::string &sourcePath)
    {
        PE_ERROR_IF(!std::filesystem::exists(sourcePath), std::string("file does not exist: " + sourcePath).c_str());

        std::filesystem::path path(sourcePath);
        FileSystem file(path.string(), std::ios::in | std::ios::ate);
        PE_ERROR_IF(!file.IsOpen(), "file could not be opened!");

        std::string code = file.ReadAll();
        file.SetReadCursor(0);
        std::string line;
        while (!file.EndOfFile())
        {
            line = file.ReadLine();
            if (line.empty())
                continue;

            size_t commentPos = line.find("//");
            if (commentPos != std::string::npos)
                line = line.substr(0, commentPos);

            // Use regex to extract include filename
            std::smatch match;
            static const std::regex includeRegex(R"(^\s*#include\s*\"([^\"]+)\"\s*$)");
            if (std::regex_search(line, match, includeRegex))
            {
                std::string includeFile = match[1].str();
                std::filesystem::path includePath = path;
                includePath.remove_filename();
                std::string includeFileFull = (includePath / includeFile).string();
                size_t posStart = code.find(line);
                if (posStart != std::string::npos)
                {
                    size_t posEnd = code.find("\n", posStart);
                    if (posEnd == std::string::npos)
                        posEnd = code.length();
                    code.erase(posStart, posEnd - posStart);
                    code.insert(posStart, ParseShader(includeFileFull));
                }
            }
        }

        return code;
    }

    void ShaderCache::Init(const std::string &sourcePath, const std::string &entryPoint, size_t definesHash)
    {
        m_sourcePath = sourcePath;
        m_code = ParseShader(m_sourcePath);

        m_hash = StringHash(m_code);
        m_hash.Combine(entryPoint);
        m_hash.Combine(definesHash);
        m_hash.Combine(static_cast<uint32_t>(RHII.GetApi()));
        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
            m_hash.Combine(RHII.GetCaps().spirvTargetVulkanVersion);
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            m_hash.Combine(4u); // DX12 source-register rewrite/reflection contract version.

        m_tempFilePath = Path::Executable;
        m_tempFilePath += "ShaderCache/";
        m_tempFilePath += (RHII.GetApi() == PE_GRAPHICS_API_DX12) ? "_dxil/" : "_spv/";
        m_tempFilePath += std::to_string(m_hash);
    }

    bool ShaderCache::ShaderNeedsCompile()
    {
        return !std::filesystem::exists(m_tempFilePath);
    }

    std::vector<uint8_t> ShaderCache::ReadBytecodeFile()
    {
        FileSystem file(m_tempFilePath, std::ios::in | std::ios::ate | std::ios::binary);
        PE_ERROR_IF(!file.IsOpen(), "failed to open file!");

        std::string buffer = file.ReadAll();

        std::vector<uint8_t> bytecode;
        bytecode.resize(file.Size());
        if (!bytecode.empty())
            memcpy(bytecode.data(), buffer.data(), file.Size());
        return bytecode;
    }

    void ShaderCache::WriteBytecodeToFile(const std::vector<uint8_t> &bytecode)
    {
        std::filesystem::path path(m_tempFilePath);
        if (!std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());

        FileSystem file(m_tempFilePath, std::ios::out | std::ios::trunc | std::ios::binary);
        PE_ERROR_IF(!file.IsOpen(), "failed to open file!");

        if (!bytecode.empty())
            file.Write(reinterpret_cast<const char *>(bytecode.data()), bytecode.size());
    }

    std::vector<uint32_t> ShaderCache::ReadSpvFile()
    {
        std::vector<uint8_t> buffer = ReadBytecodeFile();

        std::vector<uint32_t> spirv;
        spirv.resize(buffer.size() / sizeof(uint32_t));
        if (!spirv.empty())
            memcpy(spirv.data(), buffer.data(), spirv.size() * sizeof(uint32_t));

        return spirv;
    }

    void ShaderCache::WriteSpvToFile(const std::vector<uint32_t> &spirv)
    {
        std::vector<uint8_t> bytecode(spirv.size() * sizeof(uint32_t));
        if (!bytecode.empty())
            memcpy(bytecode.data(), spirv.data(), bytecode.size());
        WriteBytecodeToFile(bytecode);
    }
} // namespace pe
