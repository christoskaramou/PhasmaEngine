#include "Compute.h"
#include <deque>
#include <fstream>

using namespace vm;

vk::DescriptorSetLayout Compute::DSLayoutCompute = nullptr;

vk::DescriptorSetLayout* Compute::getDescriptorLayout()
{
	if (!DSLayoutCompute) {
		auto const setLayoutBinding = [](uint32_t binding) {
			return vk::DescriptorSetLayoutBinding{ binding, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr };
		};
		
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings {
			setLayoutBinding(0), // in
			setLayoutBinding(1)  // out
		};

		vk::DescriptorSetLayoutCreateInfo dlci;
		dlci.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		dlci.pBindings = setLayoutBindings.data();
		DSLayoutCompute = VulkanContext::get().device.createDescriptorSetLayout(dlci);
	}
	return &DSLayoutCompute;
}

Buffer& Compute::getIn()
{
	return SBIn;
}

Buffer& Compute::getOut()
{
	return SBOut;
}

void Compute::dispatch(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
{
	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	auto& cmd = commandBuffer;
	cmd.begin(beginInfo);

	//ctx.metrics[13].start(cmd);
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline.compinfo.layout, 0, DSCompute, nullptr);
	cmd.dispatch(sizeX, sizeY, sizeZ);
	//ctx.metrics[13].end(&GUI::metrics[13]);

	cmd.end();

	vk::SubmitInfo siCompute;
	siCompute.commandBufferCount = 1;
	siCompute.pCommandBuffers = &cmd;
	vulkan->computeQueue.submit(siCompute, fence);
}

void vm::Compute::waitFence()
{
	vulkan->waitFences(fence);

	ready = true;
}

void Compute::createComputeStorageBuffers(size_t sizeIn, size_t sizeOut)
{
	SBIn.createBuffer(sizeIn, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	SBIn.data = vulkan->device.mapMemory(SBIn.memory, 0, SBIn.size);
	memset(SBIn.data, 0, SBIn.size);

	SBOut.createBuffer(sizeOut, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	SBOut.data = vulkan->device.mapMemory(SBOut.memory, 0, SBOut.size);
	memset(SBOut.data, 0, SBOut.size);
}

void Compute::createDescriptorSet()
{
	vk::DescriptorSetAllocateInfo allocInfo;
	allocInfo.descriptorPool = vulkan->descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = getDescriptorLayout();
	DSCompute = vulkan->device.allocateDescriptorSets(allocInfo).at(0);
}

void vm::Compute::updateDescriptorSet()
{
	std::deque<vk::DescriptorBufferInfo> dsbi{};
	auto const wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer, vk::DescriptorType type) {
		dsbi.emplace_back(buffer.buffer, 0, buffer.size);
		return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, type, nullptr, &dsbi.back(), nullptr };
	};
	std::vector<vk::WriteDescriptorSet> writeCompDescriptorSets{
		wSetBuffer(DSCompute, 0, SBIn, vk::DescriptorType::eStorageBuffer),
		wSetBuffer(DSCompute, 1, SBOut, vk::DescriptorType::eStorageBuffer),
	};
	vulkan->device.updateDescriptorSets(writeCompDescriptorSets, nullptr);
}

void Compute::createPipeline()
{
	std::vector<char> compCode = readFile("shaders/Compute/comp.spv");
	vk::ShaderModuleCreateInfo csmci;
	csmci.codeSize = compCode.size();
	csmci.pCode = reinterpret_cast<const uint32_t*>(compCode.data());

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = getDescriptorLayout();

	auto sm = vulkan->device.createShaderModuleUnique(csmci);

	pipeline.compinfo.stage.module = sm.get();
	pipeline.compinfo.stage.pName = "main";
	pipeline.compinfo.stage.stage = vk::ShaderStageFlagBits::eCompute;
	pipeline.compinfo.layout = vulkan->device.createPipelineLayout(plci);
	pipeline.pipeline = vulkan->device.createComputePipelines(nullptr, pipeline.compinfo).at(0);
}

void Compute::destroy()
{
	SBIn.destroy();
	SBOut.destroy();
	pipeline.destroy();
	if (fence) {
		vulkan->device.destroyFence(fence);
		fence = nullptr;
	}
	if (Compute::DSLayoutCompute) {
		vulkan->device.destroyDescriptorSetLayout(Compute::DSLayoutCompute);
		Compute::DSLayoutCompute = nullptr;
	}
}

void ComputePool::Init(uint32_t cmdBuffersCount)
{
	vk::CommandPoolCreateInfo cpci;
	cpci.queueFamilyIndex = VulkanContext::get().computeFamilyId;
	cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	commandPool = VulkanContext::get().device.createCommandPool(cpci);

	vk::CommandBufferAllocateInfo cbai;
	cbai.commandPool = commandPool;
	cbai.level = vk::CommandBufferLevel::ePrimary;
	cbai.commandBufferCount = cmdBuffersCount;
	auto const cmds = VulkanContext::get().device.allocateCommandBuffers(cbai);

	for (auto& cmd : cmds) {
		compute.emplace_back();
		compute.back().commandBuffer = cmd;
		compute.back().createPipeline();
		compute.back().createDescriptorSet();
		compute.back().createComputeStorageBuffers(8000, 8000);
		compute.back().updateDescriptorSet();
		compute.back().fence = VulkanContext::get().device.createFence(vk::FenceCreateInfo());
	}
}

Compute& ComputePool::getNext()
{
	for (auto& comp : compute) {
		if (comp.ready) {
			comp.ready = false;
			return comp;
		}
	}

	// if a free compute is not found create one
	vk::CommandBufferAllocateInfo cbai;
	cbai.commandPool = commandPool;
	cbai.level = vk::CommandBufferLevel::ePrimary;
	cbai.commandBufferCount = 1;

	compute.emplace_back();
	compute.back().commandBuffer = VulkanContext::get().device.allocateCommandBuffers(cbai).at(0);
	compute.back().createPipeline();
	compute.back().createDescriptorSet();
	compute.back().createComputeStorageBuffers(8000, 8000);
	compute.back().updateDescriptorSet();
	return compute.back();
}

void ComputePool::waitFences()
{
	std::vector<vk::Fence> fences{};
	fences.reserve(compute.size());

	for (auto& comp : compute) {
		if (!comp.ready)
			fences.push_back(comp.fence);
	}

	VulkanContext::get().waitFences(fences);

	for (auto& comp : compute) {
		comp.ready = true;
	}
}

void ComputePool::destroy()
{
	for (auto& comp : compute)
		comp.destroy();
	if (commandPool) {
		VulkanContext::get().device.destroyCommandPool(commandPool);
		commandPool = nullptr;
	}
}
