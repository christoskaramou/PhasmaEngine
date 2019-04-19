#include "Skybox.h"
#include "../GUI/GUI.h"

using namespace vm;

vk::DescriptorSetLayout SkyBox::descriptorSetLayout = nullptr;

vk::DescriptorSetLayout SkyBox::getDescriptorSetLayout(vk::Device device)
{
	if (!descriptorSetLayout) {
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlags) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, stageFlags, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler,vk::ShaderStageFlagBits::eFragment),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(descriptorLayout);
	}
	return descriptorSetLayout;
}

void SkyBox::loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show)
{
	float SIZE = 1.f;
	vertices = {
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,

		-SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,

		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,

		-SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,

		-SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,

		-SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f
	};
	loadTextures(textureNames, imageSideSize);
	createVertexBuffer();
	render = show;
}

void SkyBox::update(Camera& camera)
{
	if (!render) return;

	// projection matrix correction here, because the given has reversed near and far
	mat4 projection = camera.projection;
	projection[2][2] = camera.nearPlane / (camera.nearPlane - camera.farPlane)*camera.worldOrientation.z;
	projection[3][2] = -(camera.nearPlane * camera.farPlane) / (camera.nearPlane - camera.farPlane);
	const mat4 pvm[2]{ projection, camera.view };
	memcpy(uniformBuffer.data, &pvm, sizeof(pvm));
}

void SkyBox::draw(uint32_t imageIndex)
{
	if (!render) return;

	vk::ClearColorValue clearColor;
	memcpy(clearColor.float32, GUI::clearColor.data(), 4 * sizeof(float));

	std::vector<vk::ClearValue> clearValues = { clearColor };

	vk::RenderPassBeginInfo renderPassInfo;
	renderPassInfo.renderPass = renderPass;
	renderPassInfo.framebuffer = frameBuffers[imageIndex];
	renderPassInfo.renderArea = { { 0, 0 }, vulkan->surface->actualExtent };
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	auto& cmd = vulkan->dynamicCmdBuffer;
	cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	vk::DeviceSize offset{ 0 };
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, descriptorSet, nullptr);
	cmd.bindVertexBuffers(0, vertexBuffer.buffer, offset);
	cmd.draw(static_cast<uint32_t>(vertices.size() * 0.25f), 1, 0, 0);
	cmd.endRenderPass();
}

// images must be squared and the image size must be the real else the assertion will fail
void SkyBox::loadTextures(const std::array<std::string, 6>& paths, uint32_t imageSideSize)
{
	assert(paths.size() == 6);

	texture.arrayLayers = 6;
	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.imageCreateFlags = vk::ImageCreateFlagBits::eCubeCompatible;
	texture.createImage(imageSideSize, imageSideSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

	texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	for (uint32_t i = 0; i < texture.arrayLayers; ++i) {
		// Texture Load
		int texWidth, texHeight, texChannels;
		//stbi_set_flip_vertically_on_load(true);
		stbi_uc* pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		assert(imageSideSize == texWidth && imageSideSize == texHeight);

		vk::DeviceSize imageSize = texWidth * texHeight * 4;
		if (!pixels)
			throw std::runtime_error("No pixel data loaded");

		Buffer staging;
		staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		staging.data = vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize);
		memcpy(staging.data, pixels, static_cast<size_t>(imageSize));
		vulkan->device.unmapMemory(staging.memory);
		stbi_image_free(pixels);

		texture.copyBufferToImage(staging.buffer, 0, 0, texWidth, texHeight, i);
		staging.destroy();
	}
	texture.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	texture.viewType = vk::ImageViewType::eCube;
	texture.createImageView(vk::ImageAspectFlagBits::eColor);

	texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
	texture.createSampler();
}

void SkyBox::destroy()
{
	Object::destroy();
	pipeline.destroy();
	if (SkyBox::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(SkyBox::descriptorSetLayout);
		SkyBox::descriptorSetLayout = nullptr;
	}
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
}
