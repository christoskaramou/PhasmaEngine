#include "Shader.h"
#include <fstream>
#include <iostream>
#include <filesystem>

using namespace vm;

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

Shader::Shader(const std::string& filename, ShaderType kind, bool online_compile)
{
	if (online_compile) {
		init_source(filename);
		compile_file(static_cast<shaderc_shader_kind>(kind));
	}
	else {
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

size_t Shader::size()
{
	return m_spirv.size() * sizeof(uint32_t);
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

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	options.SetIncluder(std::make_unique<FileIncluder>());
	//options.AddMacroDefinition("MY_DEFINE", "1");
	options.SetOptimizationLevel(shaderc_optimization_level_performance);

	shaderc::PreprocessedSourceCompilationResult result =
		compiler.PreprocessGlsl(m_source, kind, m_source_name.c_str(), options);

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

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	options.SetIncluder(std::make_unique<FileIncluder>());
	//options.AddMacroDefinition("MY_DEFINE", "1");
	options.SetOptimizationLevel(shaderc_optimization_level_performance);

	shaderc::AssemblyCompilationResult result = compiler.CompileGlslToSpvAssembly(
		m_source, kind, m_source_name.c_str(), options);

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

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	options.SetIncluder(std::make_unique<FileIncluder>());
	//options.AddMacroDefinition("MY_DEFINE", "1");
	options.SetOptimizationLevel(shaderc_optimization_level_performance);

	shaderc::SpvCompilationResult module =
		compiler.CompileGlslToSpv(m_source, kind, m_source_name.c_str(), options);

	if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
		std::cerr << module.GetErrorMessage();
		m_spirv = {};
	}

	m_spirv = { module.cbegin(), module.cend() };
}