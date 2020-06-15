#include "Shader.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace vm
{
	shaderc_include_result* MakeErrorIncludeResult(const char* message)
	{
		return new shaderc_include_result{ "", 0, message, strlen(message) };
	}

	shaderc_include_result* FileIncluder::GetInclude(const char* requested_source, shaderc_include_type, const char* requesting_source, size_t)
	{
		std::filesystem::path requesting_source_path(requesting_source);

		std::string full_path;
		if (requesting_source_path.is_relative())
			full_path = std::filesystem::current_path().string() + "\\" + requesting_source_path.parent_path().string() + "\\" + requested_source;
		else
			full_path = requesting_source_path.parent_path().string() + "\\" + requested_source;

		if (full_path.empty())
			return MakeErrorIncludeResult("Cannot find or open include file.");

		FileInfo* file_info = new FileInfo{
			full_path,
			[](const std::string& filename)
			{
				std::ifstream file(filename, std::ios::ate | std::ios::binary);
				if (!file.is_open()) {
					throw std::runtime_error("failed to open file!");
				}
				const size_t fileSize = static_cast<size_t>(file.tellg());
				std::vector<char> buffer(fileSize);
				file.seekg(0);
				file.read(buffer.data(), fileSize);
				file.close();

				return buffer;
			}(full_path)
		};

		included_files_.insert(full_path);

		return new shaderc_include_result{
			file_info->full_path.data(), file_info->full_path.length(),
			file_info->contents.data(), file_info->contents.size(),
			file_info };
	}

	void FileIncluder::ReleaseInclude(shaderc_include_result* include_result)
	{
		FileInfo* info = static_cast<FileInfo*>(include_result->user_data);
		delete info;
		delete include_result;
	}

	Shader::Shader(const std::string& filename, ShaderType shaderType, bool online_compile, const std::vector<Define>& defs)
	{
		this->shaderType = shaderType;
		if (online_compile)
		{
			init_source(filename);

			m_options.SetIncluder(std::make_unique<FileIncluder>());
			m_options.SetOptimizationLevel(shaderc_optimization_level_zero); // Validation Layers reporting error in spirv with other flags

			addDefines(defs);

			compile_file(static_cast<shaderc_shader_kind>(shaderType));
		}
		else
		{
			std::ifstream file(filename, std::ios::ate | std::ios::binary);
			if (!file.is_open()) {
				throw std::runtime_error("failed to open file!");
			}
			const size_t fileSize = static_cast<size_t>(file.tellg());
			std::vector<char> buffer(fileSize);
			file.seekg(0);
			file.read(buffer.data(), fileSize);
			file.close();

			m_spirv.resize(buffer.size() / sizeof(uint32_t));
			memcpy(m_spirv.data(), buffer.data(), buffer.size());
		}
	}

	const uint32_t* Shader::get_spriv()
	{
		return m_spirv.data();
	}

	ShaderType Shader::get_shader_type()
	{
		return shaderType;
	}

	size_t Shader::byte_size()
	{
		return m_spirv.size() * sizeof(uint32_t);
	}

	size_t vm::Shader::size()
	{
		return m_spirv.size();
	}

	void Shader::addDefine(Define& def)
	{
		if (def.name.empty()) return;

		if (def.value.empty())
			m_options.AddMacroDefinition(def.name);
		else
			m_options.AddMacroDefinition(def.name, def.value);

		defines.push_back(def);

	}

	void Shader::addDefines(const std::vector<Define>& defs)
	{
		for (auto def : defs)
			addDefine(def);
	}

	void Shader::init_source(const std::string& filename)
	{
		if (filename.empty())
			throw std::runtime_error("file name was empty");

		m_source_name = filename;

		std::ifstream file(m_source_name, std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("failed to open file!");
		}
		const size_t fileSize = static_cast<size_t>(file.tellg());
		m_source.clear();
		m_source.shrink_to_fit();
		m_source.resize(fileSize);
		file.seekg(0);
		file.read(m_source.data(), fileSize);
		file.close();
	}

	void Shader::preprocess_shader(shaderc_shader_kind kind)
	{
		if (m_source.empty() || m_source_name.empty())
			throw std::runtime_error("source file was empty");

		shaderc::PreprocessedSourceCompilationResult result =
			m_compiler.PreprocessGlsl(m_source, kind, m_source_name.c_str(), m_options);

		if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
			std::cerr << result.GetErrorMessage();
			m_preprocessed = "";
			return;
		}

		m_preprocessed = { result.cbegin(), result.cend() };
	}

	void Shader::compile_file_to_assembly(shaderc_shader_kind kind)
	{
		if (m_source.empty() || m_source_name.empty())
			throw std::runtime_error("source file was empty");

		shaderc::AssemblyCompilationResult result = m_compiler.CompileGlslToSpvAssembly(
			m_source, kind, m_source_name.c_str(), m_options);

		if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
			std::cerr << result.GetErrorMessage();
			m_assembly = "";
			return;
		}

		m_assembly = { result.cbegin(), result.cend() };
	}

	void Shader::compile_file(shaderc_shader_kind kind)
	{
		if (m_source.empty() || m_source_name.empty())
			throw std::runtime_error("source file was empty");

		shaderc::SpvCompilationResult module =
			m_compiler.CompileGlslToSpv(m_source, kind, m_source_name.c_str(), m_options);

		if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
			std::cerr << module.GetErrorMessage();
			m_spirv = {};
		}

		m_spirv = { module.cbegin(), module.cend() };
	}
}
