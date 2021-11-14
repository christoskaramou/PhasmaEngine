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

#include "Reflection.h"
#include "Shader.h"
#include "spirv_cross/spirv_cross.hpp"

namespace pe
{
	ShaderInOutDesc::ShaderInOutDesc()
	{
		name = "";
		location = -1;
	}

	CombinedImageSamplerDesc::CombinedImageSamplerDesc()
	{
		name = "";
		set = -1;
		binding = -1;
	}

	BufferDesc::BufferDesc()
	{
		name = "";
		set = -1;
		binding = -1;
		bufferSize = 0;
	}

	void Reflection::Init(Shader* shader)
	{
		m_shader = shader;

		spirv_cross::Compiler compiler { shader->GetSpriv(), shader->Size()};
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();
		
		auto active = compiler.get_active_interface_variables();
		compiler.set_enabled_interface_variables(std::move(active));
		
		// Shader inputs
		for (const spirv_cross::Resource& resource : resources.stage_inputs)
		{
			ShaderInOutDesc desc;
			desc.name = resource.name;
			desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
			desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));
			
			inputs.push_back(desc);
		}
		
		// Shader outputs
		for (const spirv_cross::Resource& resource : resources.stage_outputs)
		{
			ShaderInOutDesc desc;
			desc.name = resource.name;
			desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
			desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));
			
			outputs.push_back(desc);
		}
		
		// Combined image samplers
		for (const spirv_cross::Resource& resource : resources.sampled_images)
		{
			CombinedImageSamplerDesc desc;
			desc.name = resource.name;
			desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			
			samplers.push_back(desc);
		}
		
		// Uniform buffers
		for (const spirv_cross::Resource& resource : resources.uniform_buffers)
		{
			BufferDesc desc;
			desc.name = resource.name;
			desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));
			desc.bufferSize = compiler.get_declared_struct_size(*desc.type);
			
			uniformBuffers.push_back(desc);
		}
		
		// Push constants
		for (const spirv_cross::Resource& resource : resources.push_constant_buffers)
		{
			BufferDesc desc;
			desc.name = resource.name;
			desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));
			desc.bufferSize = compiler.get_declared_struct_size(*desc.type);
			
			pushConstantBuffers.push_back(desc);
		}
	}
}
