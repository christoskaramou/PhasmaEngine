/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "Shader/ShaderCache.h"

namespace pe
{
    std::string ReadFileAsString(const std::string &filename)
    {
        FileSystem file(filename, std::ios::in | std::ios::ate);
        if (file.IsOpen())
            return file.ReadAll();
        
        return "";
    }

    // Parse includes recursively and return the full code
    std::string ParseFileAndIncludes(const std::string &sourcePath)
    {
        if (!std::filesystem::exists(sourcePath))
            PE_ERROR("file does not exist!");

        std::filesystem::path path(sourcePath);
        FileSystem file(path.string(), std::ios::in | std::ios::ate);
        if (!file.IsOpen())
            PE_ERROR("file could not be opened!");
        
        std::string code = file.ReadAll();
        file.SetIndexRead(0);
        std::string line;
        while (line = file.ReadLine(), !line.empty())
        {
            size_t pos = line.find("#include");
            if (pos != std::string::npos)
            {
                std::string includeFile = line.substr(line.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                std::string includeFileFull = path.remove_filename().string() + includeFile;

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

    void ShaderCache::Init(const std::string &sourcePath)
    {
        m_sourcePath = sourcePath;
        m_code = ParseFileAndIncludes(m_sourcePath);
        m_hash = StringHash(m_code);
        m_tempFilePath = std::regex_replace(m_sourcePath, std::regex("Shaders"), "ShaderCache");
    }


    void ShaderCache::WriteToTempAsm()
    {
        std::filesystem::path path(m_tempFilePath);
        if (!std::filesystem::exists(path.remove_filename()))
            std::filesystem::create_directories(path.remove_filename());

        std::string shaderTempFile = m_tempFilePath + ".asm";

        FileSystem fileTemp(shaderTempFile, std::ios::in | std::ios::out | std::ios::trunc);
        if (!fileTemp.IsOpen())
            PE_ERROR("file could not be opened!");
        fileTemp.Write(m_assembly);
    }

    void ShaderCache::WriteToTempFile()
    {
        std::filesystem::path path(m_tempFilePath);
        if (!std::filesystem::exists(path.remove_filename()))
            std::filesystem::create_directories(path.remove_filename());

        // Remove licence spam
        size_t licenceStart = m_code.find("/*");
        while (licenceStart != std::string::npos)
        {
            size_t licenceEnd = m_code.find("*/", licenceStart);
            if (licenceEnd != std::string::npos)
                m_code.erase(licenceStart, licenceEnd - licenceStart + 2);
            else
                break;

            licenceStart = m_code.find("/*");
        }

        std::string hashStr = "// ShaderHash: " + std::to_string(m_hash) + "\n\n";

        std::string shaderTempFile = m_tempFilePath + ".temp";

        FileSystem fileTemp(shaderTempFile, std::ios::in | std::ios::out | std::ios::trunc);
        if (!fileTemp.IsOpen())
            PE_ERROR("file could not be opened!");
        
        fileTemp.Write(hashStr);
        fileTemp.Write(m_code);
    }

    size_t ShaderCache::ReadHash()
    {
        std::string shaderTempFile = m_tempFilePath + ".temp";

        FileSystem file(shaderTempFile, std::ios::in | std::ios::ate);
        if (!file.IsOpen())
            return std::numeric_limits<size_t>::max();

        std::string buffer = file.ReadAll();

        std::regex regex("\\s+");
        buffer = std::regex_replace(buffer, regex, "");

        static std::string shaderHashKey = "ShaderHash:";
        size_t pos = buffer.find(shaderHashKey);
        if (pos == std::string::npos)
            return std::numeric_limits<size_t>::max();

        std::string hashStr = buffer.substr(pos + shaderHashKey.size(), buffer.size() - pos - shaderHashKey.size());

        // string to size_t
        return std::stoull(hashStr);
    }

    bool ShaderCache::ShaderNeedsCompile()
    {
        std::string shaderSpvFile = m_tempFilePath + ".spv";
        if (!std::filesystem::exists(shaderSpvFile))
            return true;

        return m_hash != ReadHash();
    }

    std::vector <uint32_t> ShaderCache::ReadSpvFromFile()
    {
        std::string shaderSpvFile = m_tempFilePath + ".spv";
        FileSystem file(shaderSpvFile, std::ios::in | std::ios::ate | std::ios::binary);
        if (!file.IsOpen())
            PE_ERROR("failed to open file!");

        std::string buffer = file.ReadAll();

        std::vector <uint32_t> spirv;
        spirv.resize(file.Size() / sizeof(uint32_t));
        memcpy(spirv.data(), buffer.data(), file.Size());

        return spirv;
    }

    void ShaderCache::WriteSpvToFile(const std::vector <uint32_t> &spirv)
    {
        std::filesystem::path path(m_tempFilePath);
        if (!std::filesystem::exists(path.remove_filename()))
            std::filesystem::create_directories(path.remove_filename());

        std::string shaderSpvFile = m_tempFilePath + ".spv";
        FileSystem file(shaderSpvFile, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.IsOpen())
            return;

        file.Write(reinterpret_cast<const char *>(spirv.data()), spirv.size() * sizeof(uint32_t));
    }
}