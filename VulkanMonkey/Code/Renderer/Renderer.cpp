#include "Renderer.h"
#include "../Event/Event.h"

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
	if (Model::models.size() == 0) {
		if (Model::descriptorSetLayout) {
			ctx.vulkan.device.destroyDescriptorSetLayout(Model::descriptorSetLayout);
			Model::descriptorSetLayout = nullptr;
		}
		if (Mesh::descriptorSetLayout) {
			ctx.vulkan.device.destroyDescriptorSetLayout(Mesh::descriptorSetLayout);
			Mesh::descriptorSetLayout = nullptr;
		}

		if (Primitive::descriptorSetLayout) {
			ctx.vulkan.device.destroyDescriptorSetLayout(Primitive::descriptorSetLayout);
			Primitive::descriptorSetLayout = nullptr;
		}
	}
	for (auto& script : ctx.scripts)
		delete script;
	for (auto &model : Model::models)
		model.destroy();
	for (auto& texture : Mesh::uniqueTextures)
		texture.second.destroy();
	Mesh::uniqueTextures.clear();

	ctx.computePool.destroy();
	ctx.shadows.destroy();
	ctx.deferred.destroy();
	ctx.ssao.destroy();
	ctx.ssr.destroy();
	ctx.fxaa.destroy();
	ctx.bloom.destroy();
	ctx.motionBlur.destroy();
	ctx.skyBoxDay.destroy();
	ctx.skyBoxNight.destroy();
	ctx.gui.destroy();
	ctx.lightUniforms.destroy();
	for (auto& metric : ctx.metrics)
		metric.destroy();
	ctx.destroyVkContext();
}

void vm::Renderer::changeLayout(Image& image, LayoutState state)
{
	static auto transition = [](Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspectFlags) {
		vk::ImageMemoryBarrier barrier;
		barrier.image = image.image;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange = { aspectFlags, 0, image.mipLevels, 0, image.arrayLayers };
		if (image.format == vk::Format::eD32SfloatS8Uint || image.format == vk::Format::eD24UnormS8Uint) {
			barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
		}

		VulkanContext::get().dynamicCmdBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			aspectFlags == vk::ImageAspectFlagBits::eColor ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eEarlyFragmentTests,
			vk::DependencyFlagBits::eByRegion,
			nullptr,
			nullptr,
			barrier
		);
	};
	if (state != image.layoutState) {
		if (state == LayoutState::Read) {
			transition(image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
		}
		else if (state == LayoutState::Write) {
			transition(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);
		}
		else if (state == LayoutState::DepthRead) {
			transition(image, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilReadOnlyOptimal, vk::ImageAspectFlagBits::eDepth);
		}
		else if (state == LayoutState::DepthWrite) {
			transition(image, vk::ImageLayout::eDepthStencilReadOnlyOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth);
		}
		image.layoutState = state;
	}

}

void Renderer::checkQueue()
{
	for (auto it = Queue::loadModel.begin(); it != Queue::loadModel.end();) {
		VulkanContext::get().device.waitIdle();
		Queue::loadModelFutures.push_back(std::async(std::launch::async, [](const std::string& folderPath, const std::string& modelName, bool show = true) {
				Model model;
				model.loadModel(folderPath, modelName, show);
				return std::move(std::any(std::move(model)));
			}, std::get<0>(*it), std::get<1>(*it), true));
		it = Queue::loadModel.erase(it);
	}

	for (auto it = Queue::loadModelFutures.begin(); it != Queue::loadModelFutures.end();) {
		if (it->wait_for(std::chrono::seconds(0)) != std::future_status::timeout) {
			Model::models.push_back(std::move(std::any_cast<Model>(it->get())));
			GUI::modelList.push_back(Model::models.back().name);
			GUI::model_scale.push_back({ 1.f, 1.f, 1.f });
			GUI::model_pos.push_back({ 0.f, 0.f, 0.f });
			GUI::model_rot.push_back({ 0.f, 0.f, 0.f });
			it = Queue::loadModelFutures.erase(it);
		}
		else
			it++;
	}

	for (auto it = Queue::unloadModel.begin(); it != Queue::unloadModel.end();) {
		VulkanContext::get().device.waitIdle();
		Model::models[*it].destroy();
		Model::models.erase(Model::models.begin() + *it);
		GUI::modelList.erase(GUI::modelList.begin() + *it);
		GUI::model_scale.erase(GUI::model_scale.begin() + *it);
		GUI::model_pos.erase(GUI::model_pos.begin() + *it);
		GUI::model_rot.erase(GUI::model_rot.begin() + *it);
		GUI::modelItemSelected = -1;
		it = Queue::unloadModel.erase(it);
	}
#ifdef USE_SCRIPTS
	for (auto it = Queue::addScript.begin(); it != Queue::addScript.end();) {
		if (Model::models[std::get<0>(*it)].script)
			delete Model::models[std::get<0>(*it)].script;
		Model::models[std::get<0>(*it)].script = new Script(std::get<1>(*it).c_str());
		it = Queue::addScript.erase(it);
	}
	for (auto it = Queue::removeScript.begin(); it != Queue::removeScript.end();) {
		if (Model::models[*it].script) {
			delete Model::models[*it].script;
			Model::models[*it].script = nullptr;
		}
		it = Queue::removeScript.erase(it);
	}

	for (auto it = Queue::compileScript.begin(); it != Queue::compileScript.end();) {
		std::string name;
		if (Model::models[*it].script) {
			name = Model::models[*it].script->name;
			delete Model::models[*it].script;
			Model::models[*it].script = new Script(name.c_str());
		}
		it = Queue::compileScript.erase(it);
	}

#endif
}

void Renderer::update(float delta)
{
	FIRE_EVENT(Event::OnUpdate);

	// check for commands in queue
	checkQueue();

#ifdef USE_SCRIPTS
	// universal scripts
	for (auto& s : ctx.scripts)
		s->update(delta);
#endif

	// update camera matrices
	ctx.camera_main.update();

	// MODELS
	if (GUI::modelItemSelected > -1) {
		Model::models[GUI::modelItemSelected].scale = vec3(GUI::model_scale[GUI::modelItemSelected].data());
		Model::models[GUI::modelItemSelected].pos = vec3(GUI::model_pos[GUI::modelItemSelected].data());
		Model::models[GUI::modelItemSelected].rot = vec3(GUI::model_rot[GUI::modelItemSelected].data());
	}
	for (auto &model : Model::models)
		model.update(ctx.camera_main, delta);

	// GUI
	ctx.gui.update();

	// LIGHTS
	ctx.lightUniforms.update(ctx.camera_main);

	// SSAO
	ctx.ssao.update(ctx.camera_main);

	// SSR
	ctx.ssr.update(ctx.camera_main);

	// MOTION BLUR
	ctx.motionBlur.update(ctx.camera_main);

	// SHADOWS
	ctx.shadows.update(ctx.camera_main);

#ifdef RENDER_SKYBOX
	// SKYBOXES
	if (GUI::shadow_cast)
		ctx.skyBoxDay.update(ctx.camera_main);
	else
		ctx.skyBoxNight.update(ctx.camera_main);
#endif
}

void Renderer::recordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
{
	//auto beginInfo = vk::CommandBufferBeginInfo()
	//	.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
	//	.setPInheritanceInfo(nullptr);
	//
	//auto& cmd = ctx.vulkan.computeCmdBuffer;
	//cmd.begin(beginInfo);
	//
	//ctx.metrics[13].start(cmd);
	//cmd.bindPipeline(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.pipeline);
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.compinfo.layout, 0, ctx.compute.DSCompute, nullptr);
	//cmd.dispatch(sizeX, sizeY, sizeZ);
	//ctx.metrics[13].end(&GUI::metrics[13]);
	//
	//cmd.end();
}

void Renderer::recordDeferredCmds(const uint32_t& imageIndex)
{
	// TODO: Fire event to update descriptor sets based on what post proccess effects are on
	if (GUI::dSetNeedsUpdate) {
		ctx.vulkan.device.waitIdle();
		ctx.bloom.updateDescriptorSets(ctx.renderTargets);
		for (auto& fb : ctx.bloom.frameBuffers)
			ctx.vulkan.device.destroyFramebuffer(fb);
		ctx.bloom.createFrameBuffers(ctx.renderTargets);
		ctx.motionBlur.updateDescriptorSets(ctx.renderTargets);
		GUI::dSetNeedsUpdate = false;
	}

	vec2 surfSize(WIDTH_f, HEIGHT_f);
	vec2 winPos((float*)&GUI::winPos);
	vec2 winSize((float*)&GUI::winSize);

	std::vector<vec2> UVOffset{ winPos / surfSize, winSize / surfSize };

	ctx.camera_main.renderArea.update(winPos, winSize);

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	auto& cmd = ctx.vulkan.dynamicCmdBuffer;
	cmd.begin(beginInfo);
	ctx.metrics[0].start(cmd);
	cmd.setViewport(0, ctx.camera_main.renderArea.viewport);
	cmd.setScissor(0, ctx.camera_main.renderArea.scissor);

	// SKYBOX
	SkyBox& skybox = GUI::shadow_cast ? ctx.skyBoxDay : ctx.skyBoxNight;
#ifdef RENDER_SKYBOX
	ctx.metrics[1].start(cmd);
	skybox.draw(imageIndex);
	ctx.metrics[1].end(&GUI::metrics[1]);
#endif

	// MODELS
	ctx.metrics[2].start(cmd);
	ctx.deferred.batchStart(imageIndex);
	for (uint32_t m = 0; m < Model::models.size(); m++)
		Model::models[m].draw();
	ctx.deferred.batchEnd();
	ctx.metrics[2].end(&GUI::metrics[2]);
	
	changeLayout(ctx.renderTargets["albedo"], LayoutState::Read);
	changeLayout(ctx.renderTargets["depth"], LayoutState::Read);
	changeLayout(ctx.renderTargets["normal"], LayoutState::Read);
	changeLayout(ctx.renderTargets["srm"], LayoutState::Read);
	changeLayout(ctx.renderTargets["emissive"], LayoutState::Read);
	changeLayout(ctx.renderTargets["ssr"], LayoutState::Read);
	changeLayout(ctx.renderTargets["ssaoBlur"], LayoutState::Read);
	changeLayout(ctx.renderTargets["velocity"], LayoutState::Read);
	for (auto& image : ctx.shadows.textures)
		changeLayout(image, LayoutState::DepthRead);
	
	// SCREEN SPACE AMBIENT OCCLUSION
	if (GUI::show_ssao) {
		ctx.metrics[3].start(cmd);
		changeLayout(ctx.renderTargets["ssaoBlur"], LayoutState::Write);
		auto changeLayoutFunc = std::bind(&Renderer::changeLayout, this, std::placeholders::_1, std::placeholders::_2);
		ctx.ssao.draw(imageIndex, UVOffset, changeLayoutFunc, ctx.renderTargets["ssao"]);
		changeLayout(ctx.renderTargets["ssaoBlur"], LayoutState::Read);
		ctx.metrics[3].end(&GUI::metrics[3]);
	}
	
	// SCREEN SPACE REFLECTIONS
	if (GUI::show_ssr) {
		ctx.metrics[4].start(cmd);
		changeLayout(ctx.renderTargets["ssr"], LayoutState::Write);
		ctx.ssr.draw(imageIndex, UVOffset);
		changeLayout(ctx.renderTargets["ssr"], LayoutState::Read);
		ctx.metrics[4].end(&GUI::metrics[4]);
	}
	
	// COMPOSITION
	ctx.metrics[5].start(cmd);
	changeLayout(ctx.renderTargets["composition"], LayoutState::Write);
	ctx.deferred.draw(imageIndex, ctx.shadows, skybox, ctx.camera_main.invViewProjection, UVOffset);
	changeLayout(ctx.renderTargets["composition"], LayoutState::Read);
	ctx.metrics[5].end(&GUI::metrics[5]);
	
	
	// FXAA
	if (GUI::show_FXAA) {
		ctx.metrics[6].start(cmd);
		changeLayout(ctx.renderTargets["composition2"], LayoutState::Write);
		ctx.fxaa.draw(imageIndex);
		changeLayout(ctx.renderTargets["composition2"], LayoutState::Read);
		ctx.metrics[6].end(&GUI::metrics[6]);
	}
	
	// BLOOM
	if (GUI::show_Bloom) {
		ctx.metrics[7].start(cmd);
		auto changeLayoutFunc = std::bind(&Renderer::changeLayout, this, std::placeholders::_1, std::placeholders::_2);
		ctx.bloom.draw(imageIndex, (uint32_t)ctx.vulkan.swapchain->images.size(), changeLayoutFunc, ctx.renderTargets);
		ctx.metrics[7].end(&GUI::metrics[7]);
	}
	
	// MOTION BLUR
	if (GUI::show_motionBlur) {
		ctx.metrics[8].start(cmd);
		ctx.motionBlur.draw(imageIndex, UVOffset);
		ctx.metrics[8].end(&GUI::metrics[8]);
	}
	
	changeLayout(ctx.renderTargets["albedo"], LayoutState::Write);
	changeLayout(ctx.renderTargets["depth"], LayoutState::Write);
	changeLayout(ctx.renderTargets["normal"], LayoutState::Write);
	changeLayout(ctx.renderTargets["srm"], LayoutState::Write);
	changeLayout(ctx.renderTargets["emissive"], LayoutState::Write);
	changeLayout(ctx.renderTargets["ssr"], LayoutState::Write);
	changeLayout(ctx.renderTargets["ssaoBlur"], LayoutState::Write);
	changeLayout(ctx.renderTargets["velocity"], LayoutState::Write);
	for (auto& image : ctx.shadows.textures)
		changeLayout(image, LayoutState::DepthWrite);


	// GUI
	ctx.metrics[9].start(cmd);
	ctx.gui.draw(imageIndex);
	ctx.metrics[9].end(&GUI::metrics[9]);

	ctx.metrics[0].end(&GUI::metrics[0]);

	cmd.end();
}

void Renderer::recordShadowsCmds(const uint32_t& imageIndex)
{
	// Render Pass (shadows mapping) (outputs the depth image with the light POV)

	vk::DeviceSize offset = vk::DeviceSize();
	std::array<vk::ClearValue, 1> clearValuesShadows{};
	clearValuesShadows[0].depthStencil = { 0.0f, 0 };

	vk::CommandBufferBeginInfo beginInfoShadows;
	beginInfoShadows.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	vk::RenderPassBeginInfo renderPassInfoShadows;
	renderPassInfoShadows.renderPass = ctx.shadows.renderPass;
	renderPassInfoShadows.renderArea = { { 0, 0 },{ Shadows::imageSize, Shadows::imageSize } };
	renderPassInfoShadows.clearValueCount = static_cast<uint32_t>(clearValuesShadows.size());
	renderPassInfoShadows.pClearValues = clearValuesShadows.data();

	for (uint32_t i = 0; i < ctx.shadows.textures.size(); i++) {
		auto& cmd = ctx.vulkan.shadowCmdBuffer[i];
		cmd.begin(beginInfoShadows);
		ctx.metrics[10+ (size_t)i].start(cmd);
		cmd.setDepthBias(GUI::depthBias[0], GUI::depthBias[1], GUI::depthBias[2]);

		// depth[i] image ===========================================================
		renderPassInfoShadows.framebuffer = ctx.shadows.frameBuffers[i * ctx.vulkan.swapchain->images.size() + imageIndex]; // e.g. for 2 swapchain images - 1st(0,2,4) and 2nd(1,3,5)
		cmd.beginRenderPass(renderPassInfoShadows, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeline);
		for (uint32_t m = 0; m < Model::models.size(); m++) {
			if (Model::models[m].render) {
				cmd.bindVertexBuffers(0, Model::models[m].vertexBuffer.buffer, offset);
				cmd.bindIndexBuffer(Model::models[m].indexBuffer.buffer, 0, vk::IndexType::eUint32);

				for (auto& node : Model::models[m].linearNodes) {
					if (node->mesh) {
						cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeinfo.layout, 0, { ctx.shadows.descriptorSets[i], node->mesh->descriptorSet, Model::models[m].descriptorSet }, nullptr);
						for (auto& primitive : node->mesh->primitives) {
							if (primitive.render)
								cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
						}
					}
				}
			}
		}
		cmd.endRenderPass();
		ctx.metrics[10+ (size_t)i].end(&GUI::metrics[10+ (size_t)i]);
		// ==========================================================================
		cmd.end();
	}
}

void Renderer::present()
{
	if (!prepared) return;

	FIRE_EVENT(Event::OnRender);

	if (GUI::use_compute) {
		//recordComputeCmds(2, 2, 1);
		//vk::SubmitInfo siCompute;
		//siCompute.commandBufferCount = 1;
		//siCompute.setPCommandBuffers = &ctx.vulkan.computeCmdBuffer;
		//ctx.vulkan.computeQueue.submit(siCompute, ctx.vulkan.fences[1]);
		//ctx.vulkan.device.waitForFences(ctx.vulkan.fences[1], VK_TRUE, UINT64_MAX);
		//ctx.vulkan.device.resetFences(ctx.vulkan.fences[1]);
	}

	// what stage of a pipeline at a command buffer to wait for the semaphores to be done until keep going
	const vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

	uint32_t imageIndex;

	// if using shadows use the semaphore[0], record and submit the shadow commands, else use the semaphore[1]
	if (GUI::shadow_cast) {
		imageIndex = ctx.vulkan.device.acquireNextImageKHR(ctx.vulkan.swapchain->swapchain, UINT64_MAX, ctx.vulkan.semaphores[0], vk::Fence()).value;

		recordShadowsCmds(imageIndex);

		vk::SubmitInfo siShadows;
		siShadows.waitSemaphoreCount = 1;
		siShadows.pWaitSemaphores = &ctx.vulkan.semaphores[0];
		siShadows.pWaitDstStageMask = waitStages;
		siShadows.commandBufferCount = static_cast<uint32_t>(ctx.vulkan.shadowCmdBuffer.size());
		siShadows.pCommandBuffers = ctx.vulkan.shadowCmdBuffer.data();
		siShadows.signalSemaphoreCount = 1;
		siShadows.pSignalSemaphores = &ctx.vulkan.semaphores[1];
		ctx.vulkan.graphicsQueue.submit(siShadows, nullptr);
	}
	else
		imageIndex = ctx.vulkan.device.acquireNextImageKHR(ctx.vulkan.swapchain->swapchain, UINT64_MAX, ctx.vulkan.semaphores[1], vk::Fence()).value;

	recordDeferredCmds(imageIndex);

	// submit the command buffer
	vk::SubmitInfo si;
	si.waitSemaphoreCount = 1;
	si.pWaitSemaphores = &ctx.vulkan.semaphores[1];
	si.pWaitDstStageMask = waitStages;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &ctx.vulkan.dynamicCmdBuffer;
	si.signalSemaphoreCount = 1;
	si.pSignalSemaphores = &ctx.vulkan.semaphores[2];
	ctx.vulkan.graphicsQueue.submit(si, ctx.vulkan.fences[0]);

	// Presentation
	vk::PresentInfoKHR pi;
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &ctx.vulkan.semaphores[2];
	pi.swapchainCount = 1;
	pi.pSwapchains = &ctx.vulkan.swapchain->swapchain;
	pi.pImageIndices = &imageIndex;
	ctx.vulkan.presentQueue.presentKHR(pi);

	std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float> duration = start - Timer::frameStart;
	Timer::noWaitDelta = duration.count();
	ctx.vulkan.device.waitForFences(ctx.vulkan.fences[0], VK_TRUE, UINT64_MAX);
	ctx.vulkan.device.resetFences(ctx.vulkan.fences[0]);

	duration = std::chrono::high_resolution_clock::now() - start;
	waitingTime = duration.count();
}