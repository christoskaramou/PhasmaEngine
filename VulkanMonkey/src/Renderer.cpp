#include "../include/Renderer.h"
#include "../include/Errors.h"
#include "../include/Math.h"
#include <iostream>
#include <random>
#include <chrono>

Renderer::Renderer(SDL_Window* window)
{
	Context::info = &ctx;
	ctx.window = window;
	vm::vec4 v4 = -vm::vec4();
	ctx.initVulkanContext();

	ctx.skyBox = SkyBox::loadSkyBox(
		ctx.device,
		ctx.gpu,
		ctx.commandPool,
		ctx.graphicsQueue,
		ctx.descriptorPool,
		{ "objects/sky/right.png", "objects/sky/left.png", "objects/sky/top.png", "objects/sky/bottom.png", "objects/sky/back.png", "objects/sky/front.png" },
		1024
	);
	ctx.gui = GUI::loadGUI(ctx.device, ctx.gpu, ctx.dynamicCmdBuffer, ctx.graphicsQueue, ctx.descriptorPool, ctx.window, "ImGuiDemo");
	ctx.terrain = Terrain::generateTerrain(ctx.device, ctx.gpu, ctx.commandPool, ctx.graphicsQueue, ctx.descriptorPool, "");

	ctx.models.push_back(Model::loadModel(ctx.device, ctx.gpu, ctx.commandPool, ctx.graphicsQueue, ctx.descriptorPool, "objects/sponza/", "sponza.obj"));
	ctx.models.back().matrix = vm::translate(ctx.models.back().matrix, vm::vec3(2.f, 0.f, 0.f));

	ctx.shadows.createDynamicUniformBuffer(ctx.device, ctx.gpu, ctx.models.size());
	ctx.shadows.createDescriptorSet(ctx.device, ctx.descriptorPool, Shadows::getDescriptorSetLayout(ctx.device));

	{
		// some random light colors and positions
		ctx.light.resize(MAX_LIGHTS);
		for (auto& light : ctx.light) {
			light.color = vm::vec4(rand(0.f, 1.f), rand(0.0f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f));
			light.position = vm::vec4(rand(-3.5f, 3.5f), rand(.3f, 1.f), rand(-3.5f, 3.5f), 1.f);
			light.attenuation = vm::vec4(1.05f, 1.f, 1.f, 1.f);
			light.camPos = vm::vec4(0.f, 0.f, 0.f, 1.f);
		}
		ctx.light[0].color = vm::vec4(1.f, 1.f, 1.f, .5f);
		ctx.light[0].position = vm::vec4(0.f, 1.51f, -0.14f, 1.f);
		ctx.light[0].attenuation = vm::vec4(0.f, 0.f, 1.f, 1.f);
		ctx.light[0].camPos = vm::vec4(ctx.mainCamera.position, 0.0f) * vm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

		ctx.UBLights.createBuffer(ctx.device, ctx.gpu, ctx.light.size() * sizeof(Context::Light), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		VkCheck(ctx.device.mapMemory(ctx.UBLights.memory, 0, ctx.UBLights.size, vk::MemoryMapFlags(), &ctx.UBLights.data));
		memcpy(ctx.UBLights.data, ctx.light.data(), ctx.light.size() * sizeof(Context::Light));
		auto const allocateInfo = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(ctx.descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&ctx.DSLayoutLights);
		VkCheck(ctx.device.allocateDescriptorSets(&allocateInfo, &ctx.DSLights));
		auto writeSet = vk::WriteDescriptorSet()
			.setDstSet(ctx.DSLights)								// DescriptorSet dstSet;
			.setDstBinding(0)										// uint32_t dstBinding;
			.setDstArrayElement(0)									// uint32_t dstArrayElement;
			.setDescriptorCount(1)									// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)	// DescriptorType descriptorType;
			.setPBufferInfo(&vk::DescriptorBufferInfo()				// const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(ctx.UBLights.buffer)							// Buffer buffer;
				.setOffset(0)											// DeviceSize offset;
				.setRange(ctx.UBLights.size));							// DeviceSize range;
		ctx.device.updateDescriptorSets(1, &writeSet, 0, nullptr);
		std::cout << "DescriptorSet allocated and updated\n";

		// DESCRIPTOR SETS FOR COMPOSITION PIPELINE
		vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
			ctx.descriptorPool,						//DescriptorPool descriptorPool;
			1,										//uint32_t descriptorSetCount;
			&ctx.DSLayoutComposition				//const DescriptorSetLayout* pSetLayouts;
		};
		VkCheck(ctx.device.allocateDescriptorSets(&allocInfo, &ctx.DSComposition));

		// Image descriptors for the offscreen color attachments
		vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
			vk::Sampler(),					//Sampler sampler;
			ctx.position.view,				//ImageView imageView;
			vk::ImageLayout::eShaderReadOnlyOptimal		//ImageLayout imageLayout;
		};
		vk::DescriptorImageInfo texDescriptorNormal = vk::DescriptorImageInfo{
			vk::Sampler(),					//Sampler sampler;
			ctx.normal.view,				//ImageView imageView;
			vk::ImageLayout::eShaderReadOnlyOptimal		//ImageLayout imageLayout;
		};
		vk::DescriptorImageInfo texDescriptorAlbedo = vk::DescriptorImageInfo{
			vk::Sampler(),					//Sampler sampler;
			ctx.albedo.view,				//ImageView imageView;
			vk::ImageLayout::eShaderReadOnlyOptimal		//ImageLayout imageLayout;
		};
		vk::DescriptorImageInfo texDescriptorSpecular = vk::DescriptorImageInfo{
			vk::Sampler(),					//Sampler sampler;
			ctx.specular.view,				//ImageView imageView;
			vk::ImageLayout::eShaderReadOnlyOptimal		//ImageLayout imageLayout;
		};
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Position texture target
			vk::WriteDescriptorSet{
				ctx.DSComposition,						//DescriptorSet dstSet;
				0,										//uint32_t dstBinding;
				0,										//uint32_t dstArrayElement;
				1,										//uint32_t descriptorCount_;
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
				&texDescriptorPosition,					//const DescriptorImageInfo* pImageInfo;
				nullptr,								//const DescriptorBufferInfo* pBufferInfo;
				nullptr									//const BufferView* pTexelBufferView;
			},
			// Binding 1: Normals texture target
			vk::WriteDescriptorSet{
				ctx.DSComposition,						//DescriptorSet dstSet;
				1,										//uint32_t dstBinding;
				0,										//uint32_t dstArrayElement;
				1,										//uint32_t descriptorCount_;
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
				&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
				nullptr,								//const DescriptorBufferInfo* pBufferInfo;
				nullptr									//const BufferView* pTexelBufferView;
			},
			// Binding 2: Albedo texture target
			vk::WriteDescriptorSet{
				ctx.DSComposition,						//DescriptorSet dstSet;
				2,										//uint32_t dstBinding;
				0,										//uint32_t dstArrayElement;
				1,										//uint32_t descriptorCount_;
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
				&texDescriptorAlbedo,					//const DescriptorImageInfo* pImageInfo;
				nullptr,								//const DescriptorBufferInfo* pBufferInfo;
				nullptr									//const BufferView* pTexelBufferView;
			},
			// Binding 3: Specular texture target
			vk::WriteDescriptorSet{
				ctx.DSComposition,						//DescriptorSet dstSet;
				3,										//uint32_t dstBinding;
				0,										//uint32_t dstArrayElement;
				1,										//uint32_t descriptorCount_;
				vk::DescriptorType::eInputAttachment,	//DescriptorType descriptorType;
				&texDescriptorSpecular,					//const DescriptorImageInfo* pImageInfo;
				nullptr,								//const DescriptorBufferInfo* pBufferInfo;
				nullptr									//const BufferView* pTexelBufferView;
			},
			// Binding 4: Fragment shader lights
			vk::WriteDescriptorSet{
				ctx.DSComposition,						//DescriptorSet dstSet;
				4,										//uint32_t dstBinding;
				0,										//uint32_t dstArrayElement;
				1,										//uint32_t descriptorCount_;
				vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
				nullptr,								//const DescriptorImageInfo* pImageInfo;
				&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
					.setBuffer(ctx.UBLights.buffer)	// Buffer buffer;
					.setOffset(0)								// DeviceSize offset;
					.setRange(ctx.UBLights.size),	// DeviceSize range;
				nullptr									//const BufferView* pTexelBufferView;
			}
		};
		ctx.device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		std::cout << "DescriptorSet allocated and updated\n";

		// DESCRIPTOR SET FOR REFLECTION PIPELINE
		ctx.UBReflection.createBuffer(ctx.device, ctx.gpu, 256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
		VkCheck(ctx.device.mapMemory(ctx.UBReflection.memory, 0, ctx.UBReflection.size, vk::MemoryMapFlags(), &ctx.UBReflection.data));

		auto const allocateInfo2 = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(ctx.descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&ctx.DSLayoutReflection);
		VkCheck(ctx.device.allocateDescriptorSets(&allocateInfo2, &ctx.DSReflection));

		vk::WriteDescriptorSet textureWriteSets[5];
		// Albedo
		textureWriteSets[0] = vk::WriteDescriptorSet()
			.setDstSet(ctx.DSReflection)							// DescriptorSet dstSet;
			.setDstBinding(0)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(ctx.finalColor.sampler)									// Sampler sampler;
				.setImageView(ctx.finalColor.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));			// ImageLayout imageLayout;
		// Positions
		textureWriteSets[1] = vk::WriteDescriptorSet()
			.setDstSet(ctx.DSReflection)							// DescriptorSet dstSet;
			.setDstBinding(1)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(ctx.position.sampler)									// Sampler sampler;
				.setImageView(ctx.position.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));			// ImageLayout imageLayout;
		// Normals
		textureWriteSets[2] = vk::WriteDescriptorSet()
			.setDstSet(ctx.DSReflection)							// DescriptorSet dstSet;
			.setDstBinding(2)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(ctx.finalNormal.sampler)								// Sampler sampler;
				.setImageView(ctx.finalNormal.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));			// ImageLayout imageLayout;
		// Depth
		textureWriteSets[3] = vk::WriteDescriptorSet()
			.setDstSet(ctx.DSReflection)							// DescriptorSet dstSet;
			.setDstBinding(3)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(ctx.finalDepth.sampler)								// Sampler sampler;
				.setImageView(ctx.finalDepth.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal));			// ImageLayout imageLayout;
		// Uniforms
		textureWriteSets[4] = vk::WriteDescriptorSet()
			.setDstSet(ctx.DSReflection)							// DescriptorSet dstSet;
			.setDstBinding(4)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
			.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorImageInfo* pImageInfo;
				.setBuffer(ctx.UBReflection.buffer)								// Buffer buffer;
				.setOffset(0)													// DeviceSize offset;
				.setRange(3*sizeof(vm::mat4)));								// DeviceSize range;

		ctx.device.updateDescriptorSets(5, textureWriteSets, 0, nullptr);
		std::cout << "DescriptorSet allocated and updated\n";
		
		// DESCRIPTOR SET FOR COMPUTE PIPELINE
		//ctx.SBIn.createBuffer(256, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);
		//ctx.SBOut.createBuffer(256, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostCoherent);

		//vk::DescriptorSetAllocateInfo allocCompInfo = vk::DescriptorSetAllocateInfo{
		//	ctx.descriptorPool,						//DescriptorPool descriptorPool;
		//	1,										//uint32_t descriptorSetCount;
		//	&ctx.DSLayoutCompute			//const DescriptorSetLayout* pSetLayouts;
		//};
		//VkCheck(ctx.device.allocateDescriptorSets(&allocCompInfo, &ctx.DSCompute));
		//std::vector<vk::WriteDescriptorSet> writeCompDescriptorSets = {
		//	// Binding 0 (in)
		//	vk::WriteDescriptorSet{
		//		ctx.DSComposition,			//DescriptorSet dstSet;
		//		0,										//uint32_t dstBinding;
		//		0,										//uint32_t dstArrayElement;
		//		1,										//uint32_t descriptorCount_;
		//		vk::DescriptorType::eStorageBuffer,		//DescriptorType descriptorType;
		//		nullptr,								//const DescriptorImageInfo* pImageInfo;
		//		&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
		//			.setBuffer(ctx.SBIn.buffer)		// Buffer buffer;
		//			.setOffset(0)								// DeviceSize offset;
		//			.setRange(ctx.SBIn.size),		// DeviceSize range;
		//		nullptr											//const BufferView* pTexelBufferView;
		//	},
		//	// Binding 1 (out)
		//	vk::WriteDescriptorSet{
		//		ctx.DSComposition,			//DescriptorSet dstSet;
		//		0,										//uint32_t dstBinding;
		//		0,										//uint32_t dstArrayElement;
		//		1,										//uint32_t descriptorCount_;
		//		vk::DescriptorType::eStorageBuffer,		//DescriptorType descriptorType;
		//		nullptr,								//const DescriptorImageInfo* pImageInfo;
		//		&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
		//			.setBuffer(ctx.SBOut.buffer)		// Buffer buffer;
		//			.setOffset(0)								// DeviceSize offset;
		//			.setRange(ctx.SBIn.size),		// DeviceSize range;
		//		nullptr											//const BufferView* pTexelBufferView;
		//	}
		//};
	}

	// init is done
	prepared = true;
}

Renderer::~Renderer()
{
    ctx.device.waitIdle();
	for (auto& fence : ctx.fences) {
		if (fence) {
			ctx.device.destroyFence(fence);
			std::cout << "Fence destroyed\n";
		}
	}
    for (auto &semaphore : ctx.semaphores){
        if (semaphore){
            ctx.device.destroySemaphore(semaphore);
            std::cout << "Semaphore destroyed\n";
        }
    }

	ctx.pipelineCompute.destroy(ctx.device);
	ctx.pipelineReflection.destroy(ctx.device);
	ctx.pipelineComposition.destroy(ctx.device);
	ctx.pipelineDeferred.destroy(ctx.device);
	ctx.pipeline.destroy(ctx.device);
	ctx.pipelineGUI.destroy(ctx.device);
	ctx.pipelineSkyBox.destroy(ctx.device);
	ctx.pipelineShadows.destroy(ctx.device);
	ctx.pipelineTerrain.destroy(ctx.device);

	ctx.skyBox.destroy(ctx.device);
	ctx.terrain.destroy(ctx.device);
	ctx.gui.destroy(ctx.device);
	ctx.shadows.destroy(ctx.device);

    if (ctx.descriptorPool){
        ctx.device.destroyDescriptorPool(ctx.descriptorPool);
        std::cout << "DescriptorPool destroyed\n";
    }

	ctx.MSColorImage.destroy(ctx.device);
	ctx.MSDepthImage.destroy(ctx.device);
	ctx.position.destroy(ctx.device);
	ctx.normal.destroy(ctx.device);
	ctx.albedo.destroy(ctx.device);
	ctx.specular.destroy(ctx.device);
	ctx.finalColor.destroy(ctx.device);
	ctx.finalNormal.destroy(ctx.device);
	ctx.finalDepth.destroy(ctx.device);
	ctx.depth.destroy(ctx.device);


	ctx.UBLights.destroy(ctx.device);
	ctx.UBReflection.destroy(ctx.device);
	ctx.SBIn.destroy(ctx.device);
	ctx.SBOut.destroy(ctx.device);
	ctx.light.clear();
	ctx.light.shrink_to_fit();
	for (auto &model : ctx.models)
		model.destroy(ctx.device);
	ctx.models.clear();
	ctx.models.shrink_to_fit();

	if (Shadows::descriptorSetLayout) {
		ctx.device.destroyDescriptorSetLayout(Shadows::descriptorSetLayout);
		Shadows::descriptorSetLayout = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (SkyBox::descriptorSetLayout) {
		ctx.device.destroyDescriptorSetLayout(SkyBox::descriptorSetLayout);
		SkyBox::descriptorSetLayout = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (GUI::descriptorSetLayout) {
		ctx.device.destroyDescriptorSetLayout(GUI::descriptorSetLayout);
		GUI::descriptorSetLayout = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (Terrain::descriptorSetLayout) {
		ctx.device.destroyDescriptorSetLayout(Terrain::descriptorSetLayout);
		Terrain::descriptorSetLayout = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (Mesh::descriptorSetLayout) {
		ctx.device.destroyDescriptorSetLayout(Mesh::descriptorSetLayout);
		Mesh::descriptorSetLayout = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
    if (Model::descriptorSetLayout){
        ctx.device.destroyDescriptorSetLayout(Model::descriptorSetLayout);
		Model::descriptorSetLayout = nullptr;
        std::cout << "Descriptor Set Layout destroyed\n";
    }
	if (ctx.DSLayoutComposition) {
		ctx.device.destroyDescriptorSetLayout(ctx.DSLayoutComposition);
		ctx.DSLayoutComposition = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (ctx.DSLayoutReflection) {
		ctx.device.destroyDescriptorSetLayout(ctx.DSLayoutReflection);
		ctx.DSLayoutReflection = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (ctx.DSLayoutCompute) {
		ctx.device.destroyDescriptorSetLayout(ctx.DSLayoutCompute);
		ctx.DSLayoutCompute = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	if (ctx.DSLayoutLights) {
		ctx.device.destroyDescriptorSetLayout(ctx.DSLayoutLights);
		ctx.DSLayoutLights = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}

	for (auto &frameBuffer : ctx.guiFrameBuffers) {
		if (frameBuffer) {
			ctx.device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	for (auto &frameBuffer : ctx.rFrameBuffers) {
		if (frameBuffer) {
			ctx.device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	for (auto &frameBuffer : ctx.dFrameBuffers) {
		if (frameBuffer) {
			ctx.device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	for (auto &frameBuffer : ctx.frameBuffers) {
		if (frameBuffer) {
			ctx.device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}

	if (ctx.dRenderPass) {
		ctx.device.destroyRenderPass(ctx.dRenderPass);
		ctx.dRenderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	if (ctx.rRenderPass) {
		ctx.device.destroyRenderPass(ctx.rRenderPass);
		ctx.rRenderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	if (ctx.guiRenderPass) {
		ctx.device.destroyRenderPass(ctx.guiRenderPass);
		ctx.guiRenderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	if (Shadows::renderPass) {
		ctx.device.destroyRenderPass(Shadows::renderPass);
		Shadows::renderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	if (ctx.renderPass) {
		ctx.device.destroyRenderPass(ctx.renderPass);
		std::cout << "RenderPass destroyed\n";
	}

    if (ctx.commandPool){
        ctx.device.destroyCommandPool(ctx.commandPool);
        std::cout << "CommandPool destroyed\n";
    }
	
	ctx.swapchain.destroy(ctx.device);

    if (ctx.device){
        ctx.device.destroy();
        std::cout << "Device destroyed\n";
    }

    if (ctx.surface.surface){
        ctx.instance.destroySurfaceKHR(ctx.surface.surface);
        std::cout << "Surface destroyed\n";
    }

    if (ctx.instance){
		ctx.instance.destroy();
        std::cout << "Instance destroyed\n";
    }
}

void Renderer::update(float delta)
{
	const Context::ProjView proj_view{ ctx.mainCamera.getPerspective(), ctx.mainCamera.getView() };
	const vm::mat4 p_v = proj_view.projection * proj_view.view;

	//TERRAIN
	if (ctx.terrain.render) {
		const Context::UBO mvpTerrain{ proj_view.projection, proj_view.view, vm::mat4(1.0f) };
		memcpy(ctx.terrain.uniformBuffer.data, &mvpTerrain, sizeof(Context::UBO));
	}

	// MODELS
	for (auto &model : ctx.models) {
		std::sort(model.meshes.begin(), model.meshes.end(), [](Mesh& a, Mesh& b) -> bool { return a.colorEffects.diffuse.w > b.colorEffects.diffuse.w; });
		if (model.render) {
			const Context::UBO mvp{ proj_view.projection, proj_view.view, model.matrix };
			ExtractFrustum(p_v * model.matrix);
			for (auto &mesh : model.meshes) {
				mesh.cull = !SphereInFrustum(mesh.boundingSphere);
				if (!mesh.cull)
					memcpy(model.uniformBuffer.data, &mvp, sizeof(Context::UBO));
			}
		}
	}

	// SKYBOX
	if (ctx.skyBox.render)
		memcpy(ctx.skyBox.uniformBuffer.data, &proj_view, 2 * sizeof(vm::mat4));

	// SHADOWS
	const vm::vec3 lightPos = vm::vec3(ctx.light[0].position.x, ctx.light[0].position.y, ctx.light[0].position.z);
	const vm::vec3 center = -lightPos;
	std::vector<ShadowsUBO> shadows_UBO(ctx.models.size());
	for (uint32_t i = 0; i < ctx.models.size(); i++) {
		shadows_UBO[i] = { vm::ortho(-4.f, 4.f, 4.f, -4.f, 0.1f, 20.f), vm::lookAt(lightPos, center, ctx.mainCamera.worldUp), ctx.models[i].matrix, Shadows::shadowCast ? 1.0f : 0.0f };
	}
	memcpy(ctx.shadows.uniformBuffer.data, shadows_UBO.data(), sizeof(ShadowsUBO)*shadows_UBO.size());

	// GUI
	if (ctx.gui.render)
		ctx.gui.newFrame(ctx.device, ctx.gpu, ctx.window);

	//	LIGHTS // TODO: pass once
	const vm::vec4 camPos = vm::vec4(ctx.mainCamera.position, 1.0f);
	for (auto& light : ctx.light)
		light.camPos = camPos;
	memcpy(ctx.UBLights.data, ctx.light.data(), ctx.light.size() * sizeof(Context::Light));

	// REFLECTIONS
	if (ctx.SSReflections) {
		struct {
			vm::vec4 vec[4];
			vm::mat4 projection;
			vm::mat4 view;
		}reflectionInput;

		reflectionInput.vec[0] = vm::vec4(ctx.mainCamera.position, 1.0f);
		reflectionInput.vec[1] = vm::vec4(ctx.mainCamera.front, 1.0f);
		reflectionInput.vec[2] = vm::vec4(static_cast<float>(ctx.surface.actualExtent.width), static_cast<float>(ctx.surface.actualExtent.height), 0.f, 0.f);
		reflectionInput.vec[3] = vm::vec4();
		reflectionInput.projection = proj_view.projection;
		reflectionInput.view = proj_view.view;

		memcpy(ctx.UBReflection.data, &reflectionInput, sizeof(reflectionInput));
	}
}

float Renderer::rand(float x1, float x2)
{
	static auto seed = std::chrono::system_clock::now().time_since_epoch().count();
	static std::default_random_engine gen(static_cast<unsigned int>(seed));
	std::uniform_real_distribution<float> x(x1, x2);
	return x(gen);
}

void Renderer::recordForwardCmds(const uint32_t& imageIndex)
{
	// Render Pass (color)
	std::vector<vk::ClearValue> clearValues = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearDepthStencilValue({ 1.0f, 0 }) };

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(ctx.renderPass)
		.setFramebuffer(ctx.frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());

	VkCheck(ctx.dynamicCmdBuffer.begin(&beginInfo));
	ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	// TERRAIN
	ctx.terrain.draw(ctx.pipelineTerrain, ctx.dynamicCmdBuffer);
	// MODELS
	for (uint32_t m = 0; m < ctx.models.size(); m++)
		ctx.models[m].draw(ctx.pipeline, ctx.dynamicCmdBuffer, m, false, &ctx.shadows, &ctx.DSLights);
	// SKYBOX
	ctx.skyBox.draw(ctx.pipelineSkyBox, ctx.dynamicCmdBuffer);
	ctx.dynamicCmdBuffer.endRenderPass();

	// GUI
	ctx.gui.draw(ctx.guiRenderPass, ctx.guiFrameBuffers[imageIndex], ctx.surface, ctx.pipelineGUI, ctx.dynamicCmdBuffer);

	ctx.dynamicCmdBuffer.end();
}

void Renderer::recordDeferredCmds(const uint32_t& imageIndex)
{
	std::vector<vk::ClearValue> clearValues = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearDepthStencilValue({ 1.0f, 0 }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f })
	};

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(ctx.dRenderPass)
		.setFramebuffer(ctx.dFrameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());

	VkCheck(ctx.dynamicCmdBuffer.begin(&beginInfo));
	ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	vk::Viewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = static_cast<float>(ctx.surface.actualExtent.width);
	viewport.height = static_cast<float>(ctx.surface.actualExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	ctx.dynamicCmdBuffer.setViewport(0, 1, &viewport);

	vk::Rect2D scissor;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = ctx.surface.actualExtent.width;
	scissor.extent.height = ctx.surface.actualExtent.height;
	ctx.dynamicCmdBuffer.setScissor(0, 1, &scissor);

	vk::DeviceSize offset = vk::DeviceSize();

	// First Subpass
	// MODELS
	for (uint32_t m = 0; m < ctx.models.size(); m++)
		ctx.models[m].draw(ctx.pipelineDeferred, ctx.dynamicCmdBuffer, m, true);

	// Second Subpass
	ctx.dynamicCmdBuffer.nextSubpass(vk::SubpassContents::eInline);

	// Composition
	ctx.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.pipelineComposition.pipeline);
	const vk::DescriptorSet descriptorSets[] = { ctx.DSComposition, ctx.shadows.descriptorSet };
	const uint32_t dOffsets[] = { 0 };
	ctx.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.pipelineComposition.pipeinfo.layout, 0, 2, descriptorSets, 1, dOffsets);
	ctx.dynamicCmdBuffer.draw(3, 1, 0, 0);

	ctx.dynamicCmdBuffer.endRenderPass();

	// SCREEN SPACE REFLECTIONS
	if (ctx.SSReflections) {
		std::vector<vk::ClearValue> clearValues1 = {
			vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
		auto renderPassInfo1 = vk::RenderPassBeginInfo()
			.setRenderPass(ctx.rRenderPass)
			.setFramebuffer(ctx.rFrameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValues1.size()))
			.setPClearValues(clearValues1.data());
		ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfo1, vk::SubpassContents::eInline);
		ctx.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.pipelineReflection.pipeline);
		ctx.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.pipelineReflection.pipeinfo.layout, 0, 1, &ctx.DSReflection, 0, nullptr);
		ctx.dynamicCmdBuffer.draw(3, 1, 0, 0);
		ctx.dynamicCmdBuffer.endRenderPass();
	}

	// GUI
	ctx.gui.draw(ctx.guiRenderPass, ctx.guiFrameBuffers[imageIndex], ctx.surface, ctx.pipelineGUI, ctx.dynamicCmdBuffer);

	ctx.dynamicCmdBuffer.end();
}

void Renderer::recordShadowsCmds(const uint32_t& imageIndex)
{
	// Render Pass (shadows mapping) (outputs the depth image with the light POV)

	std::array<vk::ClearValue, 1> clearValuesShadows = {};
	clearValuesShadows[0].setDepthStencil({ 1.0f, 0 });
	auto beginInfoShadows = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	auto renderPassInfoShadows = vk::RenderPassBeginInfo()
		.setRenderPass(Shadows::getRenderPass(ctx.device, ctx.depth))
		.setFramebuffer(ctx.shadows.frameBuffer[imageIndex])
		.setRenderArea({ { 0, 0 },{ ctx.shadows.imageSize, ctx.shadows.imageSize } })
		.setClearValueCount(static_cast<uint32_t>(clearValuesShadows.size()))
		.setPClearValues(clearValuesShadows.data());

	VkCheck(ctx.shadowCmdBuffer.begin(&beginInfoShadows));
	ctx.shadowCmdBuffer.beginRenderPass(&renderPassInfoShadows, vk::SubpassContents::eInline);

	vk::DeviceSize offset = vk::DeviceSize();

	ctx.shadowCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.pipelineShadows.pipeline);

	for (uint32_t m = 0; m < ctx.models.size(); m++) {
		if (ctx.models[m].render) {
			ctx.shadowCmdBuffer.bindVertexBuffers(0, 1, &ctx.models[m].vertexBuffer.buffer, &offset);
			ctx.shadowCmdBuffer.bindIndexBuffer(ctx.models[m].indexBuffer.buffer, 0, vk::IndexType::eUint32);

			const uint32_t dOffsets[] = { m * sizeof(ShadowsUBO) };
			ctx.shadowCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.pipelineShadows.pipeinfo.layout, 0, 1, &ctx.shadows.descriptorSet, 1, dOffsets);

			for (auto& mesh : ctx.models[m].meshes) {
				if (mesh.render)
					ctx.shadowCmdBuffer.drawIndexed(static_cast<uint32_t>(mesh.indices.size()), 1, mesh.indexOffset, mesh.vertexOffset, 0);
			}
		}
	}

	ctx.shadowCmdBuffer.endRenderPass();
	ctx.shadowCmdBuffer.end();
}

void Renderer::ExtractFrustum(vm::mat4& projection_view_model)
{
	const float* clip = &projection_view_model[0][0];
	float t;

	/* Extract the numbers for the RIGHT plane */
	frustum[0][0] = clip[3] - clip[0];
	frustum[0][1] = clip[7] - clip[4];
	frustum[0][2] = clip[11] - clip[8];
	frustum[0][3] = clip[15] - clip[12];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[0][0] * frustum[0][0] + frustum[0][1] * frustum[0][1] + frustum[0][2] * frustum[0][2]);
	frustum[0][0] *= t;
	frustum[0][1] *= t;
	frustum[0][2] *= t;
	frustum[0][3] *= t;
	/* Extract the numbers for the LEFT plane */
	frustum[1][0] = clip[3] + clip[0];
	frustum[1][1] = clip[7] + clip[4];
	frustum[1][2] = clip[11] + clip[8];
	frustum[1][3] = clip[15] + clip[12];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[1][0] * frustum[1][0] + frustum[1][1] * frustum[1][1] + frustum[1][2] * frustum[1][2]);
	frustum[1][0] *= t;
	frustum[1][1] *= t;
	frustum[1][2] *= t;
	frustum[1][3] *= t;
	/* Extract the BOTTOM plane */
	frustum[2][0] = clip[3] + clip[1];
	frustum[2][1] = clip[7] + clip[5];
	frustum[2][2] = clip[11] + clip[9];
	frustum[2][3] = clip[15] + clip[13];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[2][0] * frustum[2][0] + frustum[2][1] * frustum[2][1] + frustum[2][2] * frustum[2][2]);
	frustum[2][0] *= t;
	frustum[2][1] *= t;
	frustum[2][2] *= t;
	frustum[2][3] *= t;
	/* Extract the TOP plane */
	frustum[3][0] = clip[3] - clip[1];
	frustum[3][1] = clip[7] - clip[5];
	frustum[3][2] = clip[11] - clip[9];
	frustum[3][3] = clip[15] - clip[13];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[3][0] * frustum[3][0] + frustum[3][1] * frustum[3][1] + frustum[3][2] * frustum[3][2]);
	frustum[3][0] *= t;
	frustum[3][1] *= t;
	frustum[3][2] *= t;
	frustum[3][3] *= t;
	/* Extract the FAR plane */
	frustum[4][0] = clip[3] - clip[2];
	frustum[4][1] = clip[7] - clip[6];
	frustum[4][2] = clip[11] - clip[10];
	frustum[4][3] = clip[15] - clip[14];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[4][0] * frustum[4][0] + frustum[4][1] * frustum[4][1] + frustum[4][2] * frustum[4][2]);
	frustum[4][0] *= t;
	frustum[4][1] *= t;
	frustum[4][2] *= t;
	frustum[4][3] *= t;
	/* Extract the NEAR plane */
	frustum[5][0] = clip[3] + clip[2];
	frustum[5][1] = clip[7] + clip[6];
	frustum[5][2] = clip[11] + clip[10];
	frustum[5][3] = clip[15] + clip[14];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[5][0] * frustum[5][0] + frustum[5][1] * frustum[5][1] + frustum[5][2] * frustum[5][2]);
	frustum[5][0] *= t;
	frustum[5][1] *= t;
	frustum[5][2] *= t;
	frustum[5][3] *= t;
}

bool Renderer::SphereInFrustum(vm::vec4& boundingSphere)
{
	for (unsigned i = 0; i < 6; i++)
		if (frustum[i][0] * boundingSphere.x + frustum[i][1] * boundingSphere.y + frustum[i][2] * boundingSphere.z + frustum[i][3] <= -boundingSphere.w)
			return false;
	return true;
}

void Renderer::present()
{
	if (!prepared) return;

	// what stage of a pipeline at a command buffer to wait for the semaphores to be done until keep going
	const vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    uint32_t imageIndex;

	// if using shadows use the semaphore[0], record and submit the shadow commands, else use the semaphore[1]
	if (Shadows::shadowCast) {
		VkCheck(ctx.device.acquireNextImageKHR(ctx.swapchain.swapchain, UINT64_MAX, ctx.semaphores[0], vk::Fence(), &imageIndex));

		recordShadowsCmds(imageIndex);

		auto const siShadows = vk::SubmitInfo()
			.setWaitSemaphoreCount(1)
			.setPWaitSemaphores(&ctx.semaphores[0])
			.setPWaitDstStageMask(waitStages)
			.setCommandBufferCount(1)
			.setPCommandBuffers(&ctx.shadowCmdBuffer)
			.setSignalSemaphoreCount(1)
			.setPSignalSemaphores(&ctx.semaphores[1]);
		VkCheck(ctx.graphicsQueue.submit(1, &siShadows, nullptr));
	}
	else
		VkCheck(ctx.device.acquireNextImageKHR(ctx.swapchain.swapchain, UINT64_MAX, ctx.semaphores[1], vk::Fence(), &imageIndex));

	if (deferredRender) {
		// use the deferred command buffer
		recordDeferredCmds(imageIndex);
	}
	else {
		// use the dynamic command buffer
		recordForwardCmds(imageIndex);
	}

	// submit the main command buffer
	auto const si = vk::SubmitInfo()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&ctx.semaphores[1])
		.setPWaitDstStageMask(waitStages)
		.setCommandBufferCount(1)
		.setPCommandBuffers(&ctx.dynamicCmdBuffer)
		.setSignalSemaphoreCount(1)
		.setPSignalSemaphores(&ctx.semaphores[2]);
	VkCheck(ctx.graphicsQueue.submit(1, &si, ctx.fences[0]));

    // Presentation
	auto const pi = vk::PresentInfoKHR()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&ctx.semaphores[2])
		.setSwapchainCount(1)
		.setPSwapchains(&ctx.swapchain.swapchain)
		.setPImageIndices(&imageIndex)
		.setPResults(nullptr); //optional
	VkCheck(ctx.presentQueue.presentKHR(&pi));

	ctx.device.waitForFences(1, &ctx.fences[0], VK_TRUE, UINT64_MAX);
	ctx.device.resetFences(1, &ctx.fences[0]);

	if (overloadedGPU)
		ctx.presentQueue.waitIdle(); // user set, when GPU can't catch the CPU commands 
}