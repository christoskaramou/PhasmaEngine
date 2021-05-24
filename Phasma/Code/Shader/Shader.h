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

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "../../Include/shaderc/shaderc.hpp"

#define ONLINE_COMPILE

namespace pe
{
	struct Define
	{
		std::string name {};
		std::string value {};
	};
	
	enum class ShaderType
	{
		Vertex,
		Fragment,
		Compute,
		Geometry,
		TessControl,
		TessEvaluation
	};
	
	class FileIncluder : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		shaderc_include_result*
		GetInclude(const char* requested_source, shaderc_include_type, const char* requesting_source, size_t) override;
		
		void ReleaseInclude(shaderc_include_result* include_result) override;
		
		inline const std::unordered_set<std::string>& file_path_trace() const
		{ return included_files_; }
	
	private:
		struct FileInfo
		{
			const std::string full_path;
			std::vector<char> contents;
		};
		std::unordered_set<std::string> included_files_;
	};
	
	class Shader
	{
	public:
		Shader(
				const std::string& filename, ShaderType shaderType, bool online_compile,
				const std::vector<Define>& defs = {}
		);
		
		const uint32_t* get_spriv();
		
		ShaderType get_shader_type();
		
		size_t byte_size();
		
		size_t size();
	
	private:
		void init_source(const std::string& filename);
		
		void preprocess_shader(shaderc_shader_kind kind);
		
		void compile_file_to_assembly(shaderc_shader_kind kind);
		
		void compile_file(shaderc_shader_kind kind);
		
		void addDefine(Define& define);
		
		void addDefines(const std::vector<Define>& defines);
		
		ShaderType shaderType;
		shaderc::Compiler m_compiler;
		shaderc::CompileOptions m_options;
		std::string m_source_name {};
		std::string m_source {};
		std::string m_preprocessed {};
		std::string m_assembly {};
		std::vector<uint32_t> m_spirv {};
		std::vector<Define> defines {};
	};
}