#include "vulkanPCH.h"
#include "Light.h"
#include "Queue.h"
#include "../GUI/GUI.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	Ref<vk::DescriptorSetLayout> LightUniforms::descriptorSetLayout = Ref<vk::DescriptorSetLayout>();

	Light::Light() :
		color(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f),
		position(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 1.f)
	{ }

	Light::Light(const vec4& color, const vec4& position) :
		color(color),
		position(position)
	{ }

	void LightUniforms::createLightUniforms()
	{
		getDescriptorSetLayout();

		uniform.createBuffer(sizeof(LightsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		uniform.map();
		uniform.copyData(&lubo);
		uniform.flush();
		uniform.unmap();

		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VulkanContext::get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = descriptorSetLayout.get();
		descriptorSet = make_ref(VulkanContext::get()->device->allocateDescriptorSets(allocateInfo).at(0));

		vk::DescriptorBufferInfo dbi;
		dbi.buffer = *uniform.buffer;
		dbi.offset = 0;
		dbi.range = uniform.size;

		vk::WriteDescriptorSet writeSet;
		writeSet.dstSet = *descriptorSet;
		writeSet.dstBinding = 0;
		writeSet.dstArrayElement = 0;
		writeSet.descriptorCount = 1;
		writeSet.descriptorType = vk::DescriptorType::eUniformBuffer;
		writeSet.pBufferInfo = &dbi;
		VulkanContext::get()->device->updateDescriptorSets(writeSet, nullptr);
	}

	void LightUniforms::destroy()
	{
		uniform.destroy();
		if (*descriptorSetLayout) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(*descriptorSetLayout);
			*descriptorSetLayout = nullptr;
		}
	}

	void LightUniforms::update(const Camera& camera)
	{
		if (GUI::randomize_lights) {

			GUI::randomize_lights = false;

			lubo = LightsUBO();
			lubo.camPos = { camera.position, 1.0f };

			Queue::memcpyRequest(&uniform, { { &lubo, sizeof(lubo), 0 } });
			//uniform.map();
			//memcpy(uniform.data, &lubo, sizeof(lubo));
			//uniform.flush();
			//uniform.unmap();

			return;

		}

		lubo.camPos = { camera.position, 1.0f };
		lubo.sun.color = { .9765f, .8431f, .9098f, GUI::sun_intensity };
		lubo.sun.position = { GUI::sun_position[0], GUI::sun_position[1], GUI::sun_position[2], 1.0f };

		Queue::memcpyRequest(&uniform, { { &lubo, 3 * sizeof(vec4), 0 } });
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
		if (!*descriptorSetLayout) {
			vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding;
			descriptorSetLayoutBinding.binding = 0;
			descriptorSetLayoutBinding.descriptorCount = 1;
			descriptorSetLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
			descriptorSetLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

			vk::DescriptorSetLayoutCreateInfo createInfo;
			createInfo.bindingCount = 1;
			createInfo.pBindings = &descriptorSetLayoutBinding;

			descriptorSetLayout = make_ref(VulkanContext::get()->device->createDescriptorSetLayout(createInfo));
		}
		return *descriptorSetLayout;
	}
}
