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
    std::string ReadFileAsString(const std::string& filename)
	{
		std::ifstream file(filename, std::ios::ate);

		if (!file.is_open())
			throw std::runtime_error("failed to open file!");

		const size_t fileSize = static_cast<size_t>(file.tellg());
		std::string buffer(fileSize, ' ');
		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();

		return buffer;
	}

	// Parse includes recursively and return the full code
	std::string ParseFileAndIncludes(const std::string& sourcePath)
	{
		if (!std::filesystem::exists(sourcePath))
			throw std::runtime_error("file does not exist!");
		
		std::filesystem::path path(sourcePath);
		std::string code = ReadFileAsString(path.string());
		std::stringstream stream(code);
		std::string line;
		while (std::getline(stream, line))
		{
			size_t pos = line.find("#include");
			if (pos != std::string::npos)
			{
				std::string includeFile = line.substr(line.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
				std::string includeFileFull = path.remove_filename().string() + includeFile;

				size_t posStart = code.find(line);
				if  (posStart != std::string::npos)
				{
					size_t posEnd = code.find("\n", posStart);
					code.erase(posStart, posEnd - posStart);
					code.insert(posStart, ParseFileAndIncludes(includeFileFull));
				}
			}
		}

		return code;
	}

    void ShaderCache::WriteToTempFile()
    {
		std::filesystem::path path(m_tempFilePath);
		if (!std::filesystem::exists(path.remove_filename()))
			std::filesystem::create_directories(path.remove_filename());

		std::string shaderTempFile = m_tempFilePath + ".temp";
		std::ofstream fileTemp(shaderTempFile, std::ios::in | std::ios::out | std::ios::trunc);
		if (!fileTemp.is_open())
			return;

		// Remove licence spam
		size_t licenceStart = m_code.find("/*");
		while(licenceStart != std::string::npos)
		{
			size_t licenceEnd = m_code.find("*/", licenceStart);
			if (licenceEnd != std::string::npos)
				m_code.erase(licenceStart, licenceEnd - licenceStart + 2);
			else
				break;

			licenceStart = m_code.find("/*");
		}
		
		std::stringstream sstream;
		sstream << "// ShaderHash: " << m_hash << "\n\n";
		fileTemp << sstream.str();
		fileTemp.write(m_code.data(), m_code.size());
		fileTemp.close();
    }

    void ShaderCache::Init(const std::string& sourcePath)
    {
        m_sourcePath = sourcePath;
		m_code = ParseFileAndIncludes(m_sourcePath);
		m_hash = StringHash(m_code);
		m_tempFilePath = std::regex_replace(m_sourcePath, std::regex("Shaders"), "ShaderCache");
    }

	size_t ShaderCache::ReadHash()
	{
		std::string shaderTempFile = m_tempFilePath + ".temp";

		std::ifstream file(shaderTempFile, std::ios::in | std::ios::ate);
		if (!file.is_open())
			return std::numeric_limits<size_t>::max();
        
		const size_t fileSize = static_cast<size_t>(file.tellg());
		std::string buffer;
		file.seekg(0);
        std::getline(file, buffer);
		file.close();

    	std::regex regex("\\s+");
    	buffer = std::regex_replace(buffer, regex, "");

		static std::string shaderHashKey = "ShaderHash:";
        size_t pos = buffer.find(shaderHashKey);
        if (pos == std::string::npos)
            return std::numeric_limits<size_t>::max();

        std::string hashStr = buffer.substr(pos + shaderHashKey.size(), buffer.size() - pos - shaderHashKey.size());

		std::stringstream sstream(hashStr);
		size_t hash;
		sstream >> hash;

		return hash;
	}
    
    bool ShaderCache::ShaderNeedsCompile()
	{
		std::string shaderSpvFile = m_tempFilePath + ".spv";
		if (!std::filesystem::exists(shaderSpvFile))
			return true;
		
		return m_hash != ReadHash();
	}

	std::vector<uint32_t> ShaderCache::ReadSpvFromFile()
	{
		std::string shaderSpvFile = m_tempFilePath + ".spv";
		std::ifstream file(shaderSpvFile, std::ios::in | std::ios::ate | std::ios::binary);
		if (!file.is_open())
			throw std::runtime_error("failed to open file!");
		
		size_t size = static_cast<size_t>(file.tellg());
		std::string buffer(size, '\0');
		file.seekg(0);
		file.read(buffer.data(), size);
		file.close();
		
        std::vector<uint32_t> spirv;
		spirv.resize(size / sizeof(uint32_t));
		memcpy(spirv.data(), buffer.data(), size);

        return spirv;
	}

	void ShaderCache::WriteSpvToFile(const std::vector<uint32_t>& spirv)
	{
		std::filesystem::path path(m_tempFilePath);
		if (!std::filesystem::exists(path.remove_filename()))
			std::filesystem::create_directories(path.remove_filename());

		std::string shaderSpvFile = m_tempFilePath + ".spv";
		std::ofstream file(shaderSpvFile, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!file.is_open())
			return;
		
		file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
		file.close();
	}
}