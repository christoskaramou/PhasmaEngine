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

#include "Shader.h"

namespace pe
{
	shaderc_include_result* MakeErrorIncludeResult(const char* message)
	{
		return new shaderc_include_result {"", 0, message, strlen(message)};
	}

	std::vector<char> ReadFile(const std::string& sourcePath)
	{
		std::ifstream file(sourcePath, std::ios::ate | std::ios::binary);

		if (!file.is_open())
			throw std::runtime_error("failed to open file!");

		const size_t fileSize = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(fileSize);
		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();

		return buffer;
	}
	
	shaderc_include_result* FileIncluder::GetInclude(const char* requested_source, shaderc_include_type, const char* requesting_source, size_t)
	{
		std::filesystem::path requesting_source_path(requesting_source);
		
		std::string full_path;
		if (requesting_source_path.is_relative())
		{
			full_path =
				std::filesystem::current_path().string() + "\\" +
				requesting_source_path.parent_path().string() + "\\" +
				requested_source;
		}
		else
			full_path = requesting_source_path.parent_path().string() + "\\" + requested_source;
		
		if (full_path.empty())
			return MakeErrorIncludeResult("Cannot find or open include file.");
		
		FileInfo* file_info = new FileInfo
		{
			full_path,
			ReadFile(full_path)
		};
		
		included_files_.insert(full_path);
		
		shaderc_include_result* inlc_result = new shaderc_include_result();
		inlc_result->source_name = file_info->full_path.data();
		inlc_result->source_name_length = file_info->full_path.length();
		inlc_result->content = file_info->contents.data();
		inlc_result->content_length = file_info->contents.size();
		inlc_result->user_data = file_info;

		return inlc_result;
	}
	
	void FileIncluder::ReleaseInclude(shaderc_include_result* include_result)
	{
		FileInfo* info = static_cast<FileInfo*>(include_result->user_data);
		delete info;
		delete include_result;
	}
	
	Shader::Shader(const std::string& sourcePath, ShaderType shaderType, const std::vector<Define>& defs)
	{
		std::string path = sourcePath;
		if (path.find(Path::Assets) == std::string::npos)
			path = Path::Assets + sourcePath;
		
		m_shaderType = shaderType;
		m_cache.Init(path);

		bool needsCompile = m_cache.ShaderNeedsCompile();
		if (needsCompile)
		{
			m_options.SetIncluder(std::make_unique<FileIncluder>());
			m_options.SetOptimizationLevel(shaderc_optimization_level_performance);
			
			AddDefines(globalDefines);
			AddDefines(defs);
			
			CompileFile(static_cast<shaderc_shader_kind>(shaderType));
			
			m_cache.WriteToTempFile();
			m_cache.WriteSpvToFile(m_spirv);
		}
		else
		{
			m_spirv = m_cache.ReadSpvFromFile();
		}

		if (m_spirv.size() > 0)
			m_reflection.Init(this);
	}
	
	void Shader::AddDefine(Define& def)
	{
		if (def.name.empty()) return;
		
		if (def.value.empty())
			m_options.AddMacroDefinition(def.name);
		else
			m_options.AddMacroDefinition(def.name, def.value);
		
		defines.push_back(def);
	}
	
	void Shader::AddDefines(const std::vector<Define>& defs)
	{
		for (auto def : defs)
			AddDefine(def);
	}

	void Shader::PreprocessShader(shaderc_shader_kind kind)
	{
		if (m_cache.GetShaderCode().empty() || m_cache.GetSourcePath().empty())
			throw std::runtime_error("source file was empty");
		
		shaderc::PreprocessedSourceCompilationResult result =
				m_compiler.PreprocessGlsl(m_cache.GetShaderCode(), kind, m_cache.GetSourcePath().c_str(), m_options);
		
		if (result.GetCompilationStatus() != shaderc_compilation_status_success)
		{
			std::cerr << result.GetErrorMessage();
			m_preprocessed = "";
			return;
		}
		
		m_preprocessed = {result.cbegin(), result.cend()};
	}
	
	void Shader::CompileFileToAssembly(shaderc_shader_kind kind)
	{
		if (m_cache.GetShaderCode().empty() || m_cache.GetSourcePath().empty())
			throw std::runtime_error("source file was empty");
		
		shaderc::AssemblyCompilationResult result = m_compiler.CompileGlslToSpvAssembly(
				m_cache.GetShaderCode(), kind, m_cache.GetSourcePath().c_str(), m_options
		);
		
		if (result.GetCompilationStatus() != shaderc_compilation_status_success)
		{
			std::cerr << result.GetErrorMessage();
			m_assembly = "";
			return;
		}
		
		m_assembly = {result.cbegin(), result.cend()};
	}
	
	void Shader::CompileFile(shaderc_shader_kind kind)
	{
		if (m_cache.GetShaderCode().empty() || m_cache.GetSourcePath().empty())
			throw std::runtime_error("source file was empty");
		
		shaderc::SpvCompilationResult module =
				m_compiler.CompileGlslToSpv(m_cache.GetShaderCode(), kind, m_cache.GetSourcePath().c_str(), m_options);
		
		if (module.GetCompilationStatus() != shaderc_compilation_status_success)
		{
			std::cerr << module.GetErrorMessage();
			m_spirv = {};
		}
		
		m_spirv = {module.cbegin(), module.cend()};
	}
}
