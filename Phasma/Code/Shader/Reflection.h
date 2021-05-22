#pragma once

#include "../Core/Base.h"
#include <vector>

namespace vk
{
	class DescriptorSetLayout;
}

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