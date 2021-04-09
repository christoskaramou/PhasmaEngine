#include "vulkanPCH.h"
#include "Reflection.h"
#include "Shader.h"
#include "../Core/Buffer.h"
#include "../MemoryHash/MemoryHash.h"
#include "../../Include/spirv_cross/spirv_cross.hpp"
#include <vector>

namespace vm
{
	Reflection::Reflection(Shader* vert, Shader* frag) : m_vert(vert), m_frag(frag)
	{
#if 1
		spirv_cross::Compiler compiler{ vert->get_spriv(), vert->size() };
		spirv_cross::ShaderResources resourses = compiler.get_shader_resources();

		auto active = compiler.get_active_interface_variables();
		compiler.set_enabled_interface_variables(std::move(active));

		// Shader inputs
		for (const spirv_cross::Resource& resource : resourses.stage_inputs)
		{
			ShaderInOutDesc desc;
			desc.name = resource.name;
			desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
			desc.type = make_ref(compiler.get_type(resource.base_type_id));

			inputs.push_back(desc);
		}

		// Shader outputs
		for (const spirv_cross::Resource& resource : resourses.stage_outputs)
		{
			ShaderInOutDesc desc;
			desc.name = resource.name;
			desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
			desc.type = make_ref(compiler.get_type(resource.base_type_id));

			outputs.push_back(desc);
		}

		// Compined image samplers
		for (const spirv_cross::Resource& resource : resourses.sampled_images)
		{
			CompinedImageSamplerDesc desc;
			desc.name = resource.name;
			desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

			samplers.push_back(desc);
		}

		// Uniform buffers
		for (const spirv_cross::Resource& resource : resourses.uniform_buffers)
		{
			BufferDesc desc;
			desc.name = resource.name;
			desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			desc.type = make_ref(compiler.get_type(resource.base_type_id));
			desc.bufferSize = compiler.get_declared_struct_size(*desc.type);

			uniformBuffers.push_back(desc);
		}

		// Push constants
		for (const spirv_cross::Resource& resource : resourses.push_constant_buffers)
		{
			BufferDesc desc;
			desc.name = resource.name;
			desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			desc.type = make_ref(compiler.get_type(resource.base_type_id));
			desc.bufferSize = compiler.get_declared_struct_size(*desc.type);

			pushConstantBuffers.push_back(desc);
		}
#endif
	}

	ShaderInOutDesc::ShaderInOutDesc()
	{
		name = "";
		location = -1;
		type = make_ref(spirv_cross::SPIRType());
	}

	CompinedImageSamplerDesc::CompinedImageSamplerDesc()
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
		type = make_ref(spirv_cross::SPIRType());
		bufferSize = 0;
	}
}
