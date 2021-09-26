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
#include "LightSystem.h"
#include "Core/Queue.h"
#include "GUI/GUI.h"
#include "Renderer/RenderApi.h"
#include "ECS/Context.h"
#include "Systems/CameraSystem.h"

namespace pe
{
	vk::DescriptorSetLayout& GetDescriptorSetLayout()
	{
		static vk::DescriptorSetLayout descriptorSetLayout;

		if (!descriptorSetLayout)
		{
			vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding;
			descriptorSetLayoutBinding.binding = 0;
			descriptorSetLayoutBinding.descriptorCount = 1;
			descriptorSetLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
			descriptorSetLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

			vk::DescriptorSetLayoutCreateInfo createInfo;
			createInfo.bindingCount = 1;
			createInfo.pBindings = &descriptorSetLayoutBinding;

			descriptorSetLayout = VulkanContext::Get()->device->createDescriptorSetLayout(createInfo);
			VulkanContext::Get()->SetDebugObjectName(descriptorSetLayout, "Lights");
		}
		return descriptorSetLayout;
	}

	LightSystem::LightSystem()
	{
	}

	LightSystem::~LightSystem()
	{
	}

	void LightSystem::Init()
	{
		uniform.CreateBuffer(
			sizeof(LightsUBO),
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		uniform.Map();
		uniform.Zero();
		uniform.Flush();
		uniform.Unmap();
		uniform.SetDebugName("Light_UB");

		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &GetDescriptorSetLayout();
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

		for (int i = 0; i < MAX_POINT_LIGHTS; i++)
		{
			PointLight& point = lubo.pointLights[i];
			point.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
			point.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
		}

		for (int i = 0; i < MAX_SPOT_LIGHTS; i++)
		{
			SpotLight& spot = lubo.spotLights[i];
			spot.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
			spot.start = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
			spot.end = spot.start + normalize(vec4(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f));
		}
		uniform.CopyRequest(Launch::Sync, { &lubo, sizeof(LightsUBO), 0 });
	}

	void LightSystem::Update(double delta)
	{
		std::vector<MemoryRange> ranges{};

		Camera& camera = *Context::Get()->GetSystem<CameraSystem>()->GetCamera(0);
		lubo.camPos = { camera.position, 1.0f };
		lubo.sun.color = { .9765f, .8431f, .9098f, GUI::sun_intensity };
		lubo.sun.direction = { GUI::sun_direction[0], GUI::sun_direction[1], GUI::sun_direction[2], 1.f };

		ranges.emplace_back(&lubo, 3 * sizeof(vec4), 0);

		if (GUI::randomize_lights)
		{
			for (int i = 0; i < MAX_POINT_LIGHTS; i++)
			{
				PointLight& point = lubo.pointLights[i];
				point.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f);
				point.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.f);
			}
			GUI::randomize_lights = false;

			ranges.emplace_back(lubo.pointLights, sizeof(PointLight) * MAX_POINT_LIGHTS, offsetof(LightsUBO, pointLights));
		}

		uniform.CopyRequest(Launch::AsyncDeferred, ranges);
	}

	void LightSystem::Destroy()
	{
		uniform.Destroy();
		if (GetDescriptorSetLayout())
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(GetDescriptorSetLayout());
			GetDescriptorSetLayout() = nullptr;
		}
	}
}
