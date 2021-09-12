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

#include "PhasmaPch.h"
#include "Light.h"
#include "../Core/Queue.h"
#include "../GUI/GUI.h"
#include "RenderApi.h"

namespace pe
{
	Ref <vk::DescriptorSetLayout> LightUniforms::descriptorSetLayout = Ref<vk::DescriptorSetLayout>();
	
	Light::Light() :
			color(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f),
			position(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 1.f)
	{}
	
	Light::Light(const vec4& color, const vec4& position) :
			color(color),
			position(position)
	{}
	
	void LightUniforms::createLightUniforms()
	{
		getDescriptorSetLayout();
		
		uniform.CreateBuffer(
			sizeof(LightsUBO),
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		uniform.Map();
		uniform.CopyData(&lubo);
		uniform.Flush();
		uniform.Unmap();
		uniform.SetDebugName("Light_UB");
		
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = descriptorSetLayout.get();
		descriptorSet = make_ref(VulkanContext::Get()->device->allocateDescriptorSets(allocateInfo).at(0));
		VulkanContext::Get()->SetDebugObjectName(*descriptorSet, "Lights");
		
		vk::DescriptorBufferInfo dbi;
		dbi.buffer = *uniform.GetBufferVK();
		dbi.offset = 0;
		dbi.range = uniform.Size();
		
		vk::WriteDescriptorSet writeSet;
		writeSet.dstSet = *descriptorSet;
		writeSet.dstBinding = 0;
		writeSet.dstArrayElement = 0;
		writeSet.descriptorCount = 1;
		writeSet.descriptorType = vk::DescriptorType::eUniformBuffer;
		writeSet.pBufferInfo = &dbi;
		VulkanContext::Get()->device->updateDescriptorSets(writeSet, nullptr);
	}
	
	void LightUniforms::destroy()
	{
		uniform.Destroy();
		if (*descriptorSetLayout)
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(*descriptorSetLayout);
			*descriptorSetLayout = nullptr;
		}
	}
	
	void LightUniforms::update(const Camera& camera)
	{
		if (GUI::randomize_lights)
		{
			
			GUI::randomize_lights = false;
			
			lubo = LightsUBO();
			lubo.camPos = {camera.position, 1.0f};
			
			Queue::memcpyRequest(&uniform, {{&lubo, sizeof(lubo), 0}});
			//uniform.map();
			//memcpy(uniform.data, &lubo, sizeof(lubo));
			//uniform.flush();
			//uniform.unmap();
			
			return;
			
		}
		
		lubo.camPos = {camera.position, 1.0f};
		lubo.sun.color = {.9765f, .8431f, .9098f, GUI::sun_intensity};
		lubo.sun.position = {GUI::sun_position[0], GUI::sun_position[1], GUI::sun_position[2], 1.0f};
		
		Queue::memcpyRequest(&uniform, {{&lubo, 3 * sizeof(vec4), 0}});
		//uniform.map();
		//memcpy(uniform.data, values, sizeof(values));
		//uniform.flush();
		//uniform.unmap();
	}
	
	LightUniforms::LightUniforms()
	{
		descriptorSetLayout = make_ref(vk::DescriptorSetLayout());
	}
	
	LightUniforms::~LightUniforms()
	{
	}
	
	const vk::DescriptorSetLayout& LightUniforms::getDescriptorSetLayout()
	{
		// binding for model mvp matrix
		if (!*descriptorSetLayout)
		{
			vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding;
			descriptorSetLayoutBinding.binding = 0;
			descriptorSetLayoutBinding.descriptorCount = 1;
			descriptorSetLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
			descriptorSetLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
			
			vk::DescriptorSetLayoutCreateInfo createInfo;
			createInfo.bindingCount = 1;
			createInfo.pBindings = &descriptorSetLayoutBinding;
			
			descriptorSetLayout = make_ref(VulkanContext::Get()->device->createDescriptorSetLayout(createInfo));
			VulkanContext::Get()->SetDebugObjectName(*descriptorSetLayout, "Lights");
		}
		return *descriptorSetLayout;
	}
}
