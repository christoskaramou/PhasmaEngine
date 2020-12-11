#include "Reflection.h"
#include "Shader.h"
#include "../Core/Buffer.h"
#include "../MemoryHash/MemoryHash.h"
#include "../../Include/spirv_cross/spirv_cross.hpp"
#include <vector>

namespace vm
{
	Reflection::Reflection(Shader* vert, Shader* frag)
		: vert(vert), frag(frag)
	{
		CreateResources();
	}


	void Reflection::CreateResources()
	{
#if testing
		spirv_cross::ShaderResources resourses;
		spirv_cross::Compiler cross_compiler{ vert->get_spriv(), vert->size() };

		auto active = cross_compiler.get_active_interface_variables();
		resourses = cross_compiler.get_shader_resources();
		cross_compiler.set_enabled_interface_variables(std::move(active));

		for (const spirv_cross::Resource& resource : resourses.stage_inputs)
		{
			auto name = resource.name;
			auto location = cross_compiler.get_decoration(resource.id, spv::DecorationLocation);
			auto type = cross_compiler.get_type(resource.base_type_id);
		}
		for (const spirv_cross::Resource& resource : resourses.stage_outputs)
		{
			auto name = resource.name;
			auto location = cross_compiler.get_decoration(resource.id, spv::DecorationLocation);
		}
		for (const spirv_cross::Resource& resource : resourses.sampled_images)
		{
			auto name = resource.name;
			auto set = cross_compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			auto binding = cross_compiler.get_decoration(resource.id, spv::DecorationBinding);
		}
		for (const spirv_cross::Resource& resource : resourses.uniform_buffers)
		{
			auto name = resource.name;
			auto set = cross_compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			auto binding = cross_compiler.get_decoration(resource.id, spv::DecorationBinding);
			auto type = cross_compiler.get_type(resource.base_type_id);
			auto bufferSize = cross_compiler.get_declared_struct_size(type);
		}
		for (const spirv_cross::Resource& resource : resourses.push_constant_buffers)
		{
			auto name = resource.name;
			auto set = cross_compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			auto binding = cross_compiler.get_decoration(resource.id, spv::DecorationBinding);
			auto type = cross_compiler.get_type(resource.base_type_id);
			auto bufferSize = cross_compiler.get_declared_struct_size(type);
		}
#endif
	}

	void Reflection::GetResources()
	{

	}
}
