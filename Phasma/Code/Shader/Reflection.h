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

#include "../Core/Base.h"
#include <vector>

namespace spirv_cross
{
	struct SPIRType;
}

namespace pe
{
	class Shader;
	
	class Reflection
	{
	public:
		class ShaderInOutDesc
		{
		public:
			ShaderInOutDesc();
			
			std::string name;
			uint32_t location = 0;
			Ref<spirv_cross::SPIRType> type;
		};
		
		class CombinedImageSamplerDesc
		{
		public:
			CombinedImageSamplerDesc();
			
			std::string name;
			uint32_t set = 0;
			uint32_t binding = 0;
		};
		
		class BufferDesc
		{
		public:
			BufferDesc();
			
			std::string name;
			uint32_t set = 0;
			uint32_t binding = 0;
			Ref<spirv_cross::SPIRType> type;
			size_t bufferSize = 0;
		};
		
		Reflection(Shader* vert, Shader* frag);
		
		std::vector<ShaderInOutDesc> inputs {};
		std::vector<ShaderInOutDesc> outputs {};
		std::vector<CombinedImageSamplerDesc> samplers {};
		std::vector<BufferDesc> uniformBuffers {};
		std::vector<BufferDesc> pushConstantBuffers {};
	
	private:
		Shader* m_vert;
		Shader* m_frag;
	};
	
}