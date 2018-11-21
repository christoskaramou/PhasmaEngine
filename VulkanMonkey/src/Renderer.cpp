#include "../include/Renderer.h"
#include "../include/Errors.h"
#include "../include/Math.h"
#include <iostream>
#include <random>
#include <chrono>

using namespace vm;

Renderer::Renderer(SDL_Window* window)
{
	ctx.vulkan.window = window;

	// INIT ALL VULKAN CONTEXT
	ctx.initVulkanContext();
	// INIT RENDERING
	ctx.initRendering();
	//LOAD RESOURCES
	ctx.loadResources();
	// CREATE UNIFORMS AND DESCRIPTOR SETS
	ctx.createUniforms();

	// init is done
	prepared = true;
}

Renderer::~Renderer()
{
    ctx.vulkan.device.waitIdle();

	for (auto &model : ctx.models)
		model.destroy();
	ctx.shadows.destroy();
	ctx.compute.destroy();
	ctx.forward.destroy();
	ctx.deferred.destroy();
	ctx.ssr.destroy();
	ctx.ssao.destroy();
	ctx.skyBox.destroy();
	ctx.terrain.destroy();
	ctx.gui.destroy();
	ctx.lightUniforms.destroy();
	ctx.destroyVkContext();
}

void Renderer::update(float delta)
{
	const vm::mat4 projection = ctx.mainCamera.getPerspective();
	const vm::mat4 view = ctx.mainCamera.getView();

	//TERRAIN
	if (ctx.terrain.render) {
		const vm::mat4 pvm[3]{ projection, view };
		memcpy(ctx.terrain.uniformBuffer.data, &pvm, sizeof(pvm));
	}

	// MODELS
	for (auto &model : ctx.models) {
		std::sort(model.meshes.begin(), model.meshes.end(), [](Mesh& a, Mesh& b) -> bool { return a.colorEffects.diffuse.w > b.colorEffects.diffuse.w; });
		if (model.render) {
			const vm::mat4 pvm[3]{ projection, view, model.matrix };
			ctx.mainCamera.ExtractFrustum(model.matrix);
			for (auto &mesh : model.meshes) {
				mesh.cull = !ctx.mainCamera.SphereInFrustum(mesh.boundingSphere);
				if (!mesh.cull)
					memcpy(model.uniformBuffer.data, &pvm, sizeof(pvm));
			}
		}
	}

	// SKYBOX
	if (ctx.skyBox.render) {
		const vm::mat4 pvm[2]{ projection, view };
		memcpy(ctx.skyBox.uniformBuffer.data, &pvm, sizeof(pvm));
	}

	// SHADOWS
	const vm::vec3 pos = Light::sun().position;
	const vm::vec3 front = vm::normalize(-pos);
	const vm::vec3 right = vm::normalize(vm::cross(front, ctx.mainCamera.worldUp()));
	const vm::vec3 up = vm::normalize(vm::cross(right, front));
	std::vector<ShadowsUBO> shadows_UBO(ctx.models.size());
	for (uint32_t i = 0; i < ctx.models.size(); i++)
		shadows_UBO[i] = { vm::ortho(-4.f, 4.f, -4.f, 4.f, 0.1f, 10.f), vm::lookAt(Light::sun().position, front, right, up), ctx.models[i].matrix, Shadows::shadowCast ? 1.0f : 0.0f };
	memcpy(ctx.shadows.uniformBuffer.data, shadows_UBO.data(), sizeof(ShadowsUBO)*shadows_UBO.size());

	// GUI
	if (ctx.gui.render)
		ctx.gui.newFrame(ctx.vulkan.device, ctx.vulkan.gpu, ctx.vulkan.window);

	// LIGHTS
	vm::vec4 camPos(ctx.mainCamera.position, 1.0f);
	memcpy(ctx.lightUniforms.uniform.data, &camPos, sizeof(vm::vec4));

	// SSAO
	if (useSSAO) {
		vm::mat4 pvm[2]{ projection, view };
		memcpy(ctx.ssao.UBssaoPVM.data, pvm, sizeof(pvm));
	}

	// REFLECTIONS
	if (useSSR) {
		vm::mat4 reflectionInput[3];
		reflectionInput[0][0] = vm::vec4(ctx.mainCamera.position, 1.0f);
		reflectionInput[0][1] = vm::vec4(ctx.mainCamera.front(), 1.0f);
		reflectionInput[0][2] = vm::vec4(static_cast<float>(ctx.vulkan.surface->actualExtent.width), static_cast<float>(ctx.vulkan.surface->actualExtent.height), 0.f, 0.f);
		reflectionInput[0][3] = vm::vec4();
		reflectionInput[1] = projection;
		reflectionInput[2] = view;
		memcpy(ctx.ssr.UBReflection.data, &reflectionInput, sizeof(reflectionInput));
	}
}

void Renderer::recordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
{
	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	VkCheck(ctx.vulkan.computeCmdBuffer.begin(&beginInfo));

	ctx.vulkan.computeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.pipeline);
	ctx.vulkan.computeCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.compinfo.layout, 0, 1, &ctx.compute.DSCompute, 0, nullptr);

	ctx.vulkan.computeCmdBuffer.dispatch(sizeX, sizeY, sizeZ);
	ctx.vulkan.computeCmdBuffer.end();
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
		.setRenderPass(ctx.forward.renderPass)
		.setFramebuffer(ctx.forward.frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.vulkan.surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());

	VkCheck(ctx.vulkan.dynamicCmdBuffer.begin(&beginInfo));
	ctx.vulkan.dynamicCmdBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	// TERRAIN
	ctx.terrain.draw(ctx.vulkan.dynamicCmdBuffer);
	// MODELS
	for (uint32_t m = 0; m < ctx.models.size(); m++)
		ctx.models[m].draw(ctx.forward.pipeline, ctx.vulkan.dynamicCmdBuffer, m, false, &ctx.shadows, &ctx.lightUniforms.descriptorSet);
	// SKYBOX
	ctx.skyBox.draw(ctx.vulkan.dynamicCmdBuffer);
	ctx.vulkan.dynamicCmdBuffer.endRenderPass();

	// GUI
	ctx.gui.draw(ctx.gui.renderPass, ctx.gui.frameBuffers[imageIndex], ctx.gui.pipeline, ctx.vulkan.dynamicCmdBuffer);

	ctx.vulkan.dynamicCmdBuffer.end();
}

void Renderer::recordDeferredCmds(const uint32_t& imageIndex)
{

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	VkCheck(ctx.vulkan.dynamicCmdBuffer.begin(&beginInfo));

	// Begin deferred
	std::vector<vk::ClearValue> clearValues = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearDepthStencilValue({ 1.0f, 0 }) };
	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(ctx.deferred.renderPass)
		.setFramebuffer(ctx.deferred.frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.vulkan.surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());

	ctx.vulkan.dynamicCmdBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	vk::Viewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = static_cast<float>(ctx.vulkan.surface->actualExtent.width);
	viewport.height = static_cast<float>(ctx.vulkan.surface->actualExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	ctx.vulkan.dynamicCmdBuffer.setViewport(0, 1, &viewport);

	vk::Rect2D scissor;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = ctx.vulkan.surface->actualExtent.width;
	scissor.extent.height = ctx.vulkan.surface->actualExtent.height;
	ctx.vulkan.dynamicCmdBuffer.setScissor(0, 1, &scissor);

	// MODELS
	for (uint32_t m = 0; m < ctx.models.size(); m++)
		ctx.models[m].draw(ctx.deferred.pipeline, ctx.vulkan.dynamicCmdBuffer, m, true);
	ctx.vulkan.dynamicCmdBuffer.endRenderPass();
	// End deferred

	// Begin SCREEN SPACE AMBIENT OCCLUSION
	if (useSSAO) {
		// SSAO image
		std::vector<vk::ClearValue> clearValuesSSAO = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
		auto renderPassInfoSSAO = vk::RenderPassBeginInfo()
			.setRenderPass(ctx.ssao.renderPass)
			.setFramebuffer(ctx.ssao.frameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, ctx.vulkan.surface->actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValuesSSAO.size()))
			.setPClearValues(clearValuesSSAO.data());
		ctx.vulkan.dynamicCmdBuffer.beginRenderPass(&renderPassInfoSSAO, vk::SubpassContents::eInline);
		ctx.vulkan.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipeline.pipeline);
		const vk::DescriptorSet descriptorSets[] = { ctx.ssao.DSssao };
		const uint32_t dOffsets[] = { 0 };
		ctx.vulkan.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipeline.pipeinfo.layout, 0, 1, descriptorSets, 0, dOffsets);
		ctx.vulkan.dynamicCmdBuffer.draw(3, 1, 0, 0);
		ctx.vulkan.dynamicCmdBuffer.endRenderPass();
		
		// new blurry SSAO image
		std::vector<vk::ClearValue> clearValuesSSAOBlur = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
		auto renderPassInfoSSAOBlur = vk::RenderPassBeginInfo()
			.setRenderPass(ctx.ssao.blurRenderPass)
			.setFramebuffer(ctx.ssao.blurFrameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, ctx.vulkan.surface->actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValuesSSAOBlur.size()))
			.setPClearValues(clearValuesSSAOBlur.data());
		ctx.vulkan.dynamicCmdBuffer.beginRenderPass(&renderPassInfoSSAOBlur, vk::SubpassContents::eInline);
		ctx.vulkan.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipelineBlur.pipeline);
		const vk::DescriptorSet descriptorSetsBlur[] = { ctx.ssao.DSssaoBlur };
		const uint32_t dOffsetsBlur[] = { 0 };
		ctx.vulkan.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipelineBlur.pipeinfo.layout, 0, 1, descriptorSetsBlur, 0, dOffsetsBlur);
		ctx.vulkan.dynamicCmdBuffer.draw(3, 1, 0, 0);
		ctx.vulkan.dynamicCmdBuffer.endRenderPass();
	}
	// End SCREEN SPACE AMBIENT OCCLUSION

	// Begin Composition
	std::vector<vk::ClearValue> clearValues0 = {
	vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
	auto renderPassInfo0 = vk::RenderPassBeginInfo()
		.setRenderPass(ctx.deferred.compositionRenderPass)
		.setFramebuffer(ctx.deferred.compositionFrameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.vulkan.surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues0.size()))
		.setPClearValues(clearValues0.data());
	ctx.vulkan.dynamicCmdBuffer.beginRenderPass(&renderPassInfo0, vk::SubpassContents::eInline);
	float ssao = useSSAO ? 1.f : 0.f;
	ctx.vulkan.dynamicCmdBuffer.pushConstants(ctx.deferred.pipelineComposition.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(float), &ssao);
	ctx.vulkan.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.deferred.pipelineComposition.pipeline);
	const vk::DescriptorSet descriptorSets[] = { ctx.deferred.DSComposition, ctx.shadows.descriptorSet };
	const uint32_t dOffsets[] = { 0 };
	ctx.vulkan.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.deferred.pipelineComposition.pipeinfo.layout, 0, 2, descriptorSets, 1, dOffsets);
	ctx.vulkan.dynamicCmdBuffer.draw(3, 1, 0, 0);
	ctx.vulkan.dynamicCmdBuffer.endRenderPass();
	// End Composition

	// SCREEN SPACE REFLECTIONS
	if (useSSR) {
		std::vector<vk::ClearValue> clearValues1 = {
			vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
		auto renderPassInfo1 = vk::RenderPassBeginInfo()
			.setRenderPass(ctx.ssr.renderPass)
			.setFramebuffer(ctx.ssr.frameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, ctx.vulkan.surface->actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValues1.size()))
			.setPClearValues(clearValues1.data());
		ctx.vulkan.dynamicCmdBuffer.beginRenderPass(&renderPassInfo1, vk::SubpassContents::eInline);
		ctx.vulkan.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.ssr.pipeline.pipeline);
		ctx.vulkan.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.ssr.pipeline.pipeinfo.layout, 0, 1, &ctx.ssr.DSReflection, 0, nullptr);
		ctx.vulkan.dynamicCmdBuffer.draw(3, 1, 0, 0);
		ctx.vulkan.dynamicCmdBuffer.endRenderPass();
	}

	// GUI
	ctx.gui.draw(ctx.gui.renderPass, ctx.gui.frameBuffers[imageIndex], ctx.gui.pipeline, ctx.vulkan.dynamicCmdBuffer);

	ctx.vulkan.dynamicCmdBuffer.end();
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
		.setRenderPass(ctx.shadows.getRenderPass())
		.setFramebuffer(ctx.shadows.frameBuffer[imageIndex])
		.setRenderArea({ { 0, 0 },{ ctx.shadows.imageSize, ctx.shadows.imageSize } })
		.setClearValueCount(static_cast<uint32_t>(clearValuesShadows.size()))
		.setPClearValues(clearValuesShadows.data());

	VkCheck(ctx.vulkan.shadowCmdBuffer.begin(&beginInfoShadows));
	ctx.vulkan.shadowCmdBuffer.beginRenderPass(&renderPassInfoShadows, vk::SubpassContents::eInline);

	vk::DeviceSize offset = vk::DeviceSize();

	ctx.vulkan.shadowCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeline);

	for (uint32_t m = 0; m < ctx.models.size(); m++) {
		if (ctx.models[m].render) {
			ctx.vulkan.shadowCmdBuffer.bindVertexBuffers(0, 1, &ctx.models[m].vertexBuffer.buffer, &offset);
			ctx.vulkan.shadowCmdBuffer.bindIndexBuffer(ctx.models[m].indexBuffer.buffer, 0, vk::IndexType::eUint32);

			const uint32_t dOffsets[] = { m * sizeof(ShadowsUBO) };
			ctx.vulkan.shadowCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeinfo.layout, 0, 1, &ctx.shadows.descriptorSet, 1, dOffsets);

			for (auto& mesh : ctx.models[m].meshes) {
				if (mesh.render)
					ctx.vulkan.shadowCmdBuffer.drawIndexed(static_cast<uint32_t>(mesh.indices.size()), 1, mesh.indexOffset, mesh.vertexOffset, 0);
			}
		}
	}

	ctx.vulkan.shadowCmdBuffer.endRenderPass();
	ctx.vulkan.shadowCmdBuffer.end();
}

void Renderer::present()
{
	if (!prepared) return;

	if (useCompute) {
		recordComputeCmds(2, 2, 1);
		auto const siCompute = vk::SubmitInfo()
			.setCommandBufferCount(1)
			.setPCommandBuffers(&ctx.vulkan.computeCmdBuffer);
		VkCheck(ctx.vulkan.computeQueue.submit(1, &siCompute, ctx.vulkan.fences[1]));
		ctx.vulkan.device.waitForFences(1, &ctx.vulkan.fences[1], VK_TRUE, UINT64_MAX);
		ctx.vulkan.device.resetFences(1, &ctx.vulkan.fences[1]);
	}

	// what stage of a pipeline at a command buffer to wait for the semaphores to be done until keep going
	const vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    uint32_t imageIndex;

	// if using shadows use the semaphore[0], record and submit the shadow commands, else use the semaphore[1]
	if (Shadows::shadowCast) {
		VkCheck(ctx.vulkan.device.acquireNextImageKHR(ctx.vulkan.swapchain->swapchain, UINT64_MAX, ctx.vulkan.semaphores[0], vk::Fence(), &imageIndex));

		recordShadowsCmds(imageIndex);

		auto const siShadows = vk::SubmitInfo()
			.setWaitSemaphoreCount(1)
			.setPWaitSemaphores(&ctx.vulkan.semaphores[0])
			.setPWaitDstStageMask(waitStages)
			.setCommandBufferCount(1)
			.setPCommandBuffers(&ctx.vulkan.shadowCmdBuffer)
			.setSignalSemaphoreCount(1)
			.setPSignalSemaphores(&ctx.vulkan.semaphores[1]);
		VkCheck(ctx.vulkan.graphicsQueue.submit(1, &siShadows, nullptr));
	}
	else
		VkCheck(ctx.vulkan.device.acquireNextImageKHR(ctx.vulkan.swapchain->swapchain, UINT64_MAX, ctx.vulkan.semaphores[1], vk::Fence(), &imageIndex));

	if (useDeferredRender) {
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
		.setPWaitSemaphores(&ctx.vulkan.semaphores[1])
		.setPWaitDstStageMask(waitStages)
		.setCommandBufferCount(1)
		.setPCommandBuffers(&ctx.vulkan.dynamicCmdBuffer)
		.setSignalSemaphoreCount(1)
		.setPSignalSemaphores(&ctx.vulkan.semaphores[2]);
	VkCheck(ctx.vulkan.graphicsQueue.submit(1, &si, ctx.vulkan.fences[0]));

    // Presentation
	auto const pi = vk::PresentInfoKHR()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&ctx.vulkan.semaphores[2])
		.setSwapchainCount(1)
		.setPSwapchains(&ctx.vulkan.swapchain->swapchain)
		.setPImageIndices(&imageIndex)
		.setPResults(nullptr); //optional
	VkCheck(ctx.vulkan.presentQueue.presentKHR(&pi));

	ctx.vulkan.device.waitForFences(1, &ctx.vulkan.fences[0], VK_TRUE, UINT64_MAX);
	ctx.vulkan.device.resetFences(1, &ctx.vulkan.fences[0]);

	if (overloadedGPU)
		ctx.vulkan.presentQueue.waitIdle(); // user set, when GPU can't catch the CPU commands 
}