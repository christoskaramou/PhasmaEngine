#include "vulkanPCH.h"
#include "Renderer.h"
#include "../Core/Queue.h"
#include "../Model/Mesh.h"
#include "../VulkanContext/VulkanContext.h"
#include "../Camera/Camera.h"
#include "../Context/Context.h"

namespace vm
{
	Renderer::Renderer(Context* ctx, SDL_Window* window)
	{
		// Temporary ugliness, until ECS is complete
		this->window = window;
		this->ctx = ctx;
		VulkanContext::get()->window = window;
	}

	void Renderer::Init()
	{
		auto& vulkan = *VulkanContext::get();

		// INIT VULKAN CONTEXT
		vulkan.Init(ctx);
		// INIT RENDERING
		AddRenderTarget("viewport", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);
		AddRenderTarget("depth", vk::Format::eR32Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("albedo", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("srm", vulkan.surface.formatKHR->format, vk::ImageUsageFlags()); // Specular Roughness Metallic
		AddRenderTarget("ssao", vk::Format::eR16Unorm, vk::ImageUsageFlags());
		AddRenderTarget("ssaoBlur", vk::Format::eR8Unorm, vk::ImageUsageFlags());
		AddRenderTarget("ssr", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("velocity", vk::Format::eR16G16Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("brightFilter", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("gaussianBlurHorizontal", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("gaussianBlurVertical", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("emissive", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("taa", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);

		taa.Init();
		bloom.Init();
		fxaa.Init();
		motionBlur.Init();
		dof.Init();

		// render passes
		shadows.createRenderPass();
		ssao.createRenderPasses(renderTargets);
		ssr.createRenderPass(renderTargets);
		deferred.createRenderPasses(renderTargets);
		fxaa.createRenderPass(renderTargets);
		taa.createRenderPasses(renderTargets);
		bloom.createRenderPasses(renderTargets);
		dof.createRenderPass(renderTargets);
		motionBlur.createRenderPass(renderTargets);
		gui.createRenderPass();

		// frame buffers
		shadows.createFrameBuffers();
		ssao.createFrameBuffers(renderTargets);
		ssr.createFrameBuffers(renderTargets);
		deferred.createFrameBuffers(renderTargets);
		fxaa.createFrameBuffers(renderTargets);
		taa.createFrameBuffers(renderTargets);
		bloom.createFrameBuffers(renderTargets);
		dof.createFrameBuffers(renderTargets);
		motionBlur.createFrameBuffers(renderTargets);
		gui.createFrameBuffers();

		// pipelines
		shadows.createPipeline();
		ssao.createPipelines(renderTargets);
		ssr.createPipeline(renderTargets);
		deferred.createPipelines(renderTargets);
		fxaa.createPipeline(renderTargets);
		taa.createPipelines(renderTargets);
		bloom.createPipelines(renderTargets);
		dof.createPipeline(renderTargets);
		motionBlur.createPipeline(renderTargets);
		gui.createPipeline();

		ComputePool::get()->Init(5);

		metrics.resize(20);
		//LOAD RESOURCES
		LoadResources();
		// CREATE UNIFORMS AND DESCRIPTOR SETS
		CreateUniforms();
	}

	Renderer::~Renderer()
	{
		VulkanContext::get()->device->waitIdle();

		Destroy();

		if (Model::models.empty()) {
			if (Pipeline::getDescriptorSetLayoutModel()) {
				VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutModel());
				Pipeline::getDescriptorSetLayoutModel() = nullptr;
			}
			if (Pipeline::getDescriptorSetLayoutMesh()) {
				VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutMesh());
				Pipeline::getDescriptorSetLayoutMesh() = nullptr;
			}
			if (Pipeline::getDescriptorSetLayoutPrimitive()) {
				VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutPrimitive());
				Pipeline::getDescriptorSetLayoutPrimitive() = nullptr;
			}
		}

#ifndef IGNORE_SCRIPTS
		for (auto& script : scripts)
			delete script;
#endif
		for (auto& model : Model::models)
			model.destroy();
		for (auto& texture : Mesh::uniqueTextures)
			texture.second.destroy();
		Mesh::uniqueTextures.clear();

		ComputePool::get()->destroy();
		ComputePool::remove();
		shadows.destroy();
		deferred.destroy();
		ssao.destroy();
		ssr.destroy();
		fxaa.destroy();
		taa.destroy();
		bloom.destroy();
		dof.destroy();
		motionBlur.destroy();
		skyBoxDay.destroy();
		skyBoxNight.destroy();
		gui.destroy();
		lightUniforms.destroy();
		for (auto& metric : metrics)
			metric.destroy();
		ctx->GetVKContext()->Destroy();
		ctx->GetVKContext()->remove();
	}

	void Renderer::CheckQueue() const
	{
		for (auto it = Queue::loadModel.begin(); it != Queue::loadModel.end();) {
			VulkanContext::get()->device->waitIdle();
			Queue::loadModelFutures.push_back(std::async(std::launch::async, [](const std::string& folderPath, const std::string& modelName, bool show = true) {
				Model model;
				model.loadModel(folderPath, modelName, show);
				for (auto& _model : Model::models)
					if (_model.name == model.name)
						model.name = "_" + model.name;
				return std::any(std::move(model));
				}, std::get<0>(*it), std::get<1>(*it), true));
			it = Queue::loadModel.erase(it);
		}

		for (auto it = Queue::loadModelFutures.begin(); it != Queue::loadModelFutures.end();) {
			if (it->wait_for(std::chrono::seconds(0)) != std::future_status::timeout) {
				Model::models.push_back(std::any_cast<Model>(it->get()));
				GUI::modelList.push_back(Model::models.back().name);
				GUI::model_scale.push_back({ 1.f, 1.f, 1.f });
				GUI::model_pos.push_back({ 0.f, 0.f, 0.f });
				GUI::model_rot.push_back({ 0.f, 0.f, 0.f });
				it = Queue::loadModelFutures.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = Queue::unloadModel.begin(); it != Queue::unloadModel.end();) {
			VulkanContext::get()->device->waitIdle();
			Model::models[*it].destroy();
			Model::models.erase(Model::models.begin() + *it);
			GUI::modelList.erase(GUI::modelList.begin() + *it);
			GUI::model_scale.erase(GUI::model_scale.begin() + *it);
			GUI::model_pos.erase(GUI::model_pos.begin() + *it);
			GUI::model_rot.erase(GUI::model_rot.begin() + *it);
			GUI::modelItemSelected = -1;
			it = Queue::unloadModel.erase(it);
		}
#ifndef IGNORE_SCRIPTS
		for (auto it = Queue::addScript.begin(); it != Queue::addScript.end();) {
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

	void Renderer::Update(double delta)
	{
		static Timer timer;
		timer.Start();

		// check for commands in queue
		CheckQueue();

#ifndef IGNORE_SCRIPTS
		// universal scripts
		for (auto& s : scripts)
			s->update(static_cast<float>(delta));
#endif

		CameraSystem* cameraSystem = ctx->GetSystem<CameraSystem>();
		Camera& camera_main = cameraSystem->GetCamera(0);

		// Model updates + 8(the rest updates)
		std::vector<std::future<void>> futureUpdates;
		futureUpdates.reserve(Model::models.size() + 8);

		// MODELS
		if (GUI::modelItemSelected > -1) {
			Model::models[GUI::modelItemSelected].scale = vec3(GUI::model_scale[GUI::modelItemSelected].data());
			Model::models[GUI::modelItemSelected].pos = vec3(GUI::model_pos[GUI::modelItemSelected].data());
			Model::models[GUI::modelItemSelected].rot = vec3(GUI::model_rot[GUI::modelItemSelected].data());
		}
		for (auto& model : Model::models)
		{
			const auto updateModel = [&]() { model.update(camera_main, delta); };
			futureUpdates.push_back(std::async(std::launch::async, updateModel));
		}

		// GUI
		auto updateGUI = [&]() { gui.update(); };
		futureUpdates.push_back(std::async(std::launch::async, updateGUI));

		// LIGHTS
		auto updateLights = [&]() { lightUniforms.update(camera_main); };
		futureUpdates.push_back(std::async(std::launch::async, updateLights));

		// SSAO
		auto updateSSAO = [&]() { ssao.update(camera_main); };
		futureUpdates.push_back(std::async(std::launch::async, updateSSAO));

		// SSR
		auto updateSSR = [&]() { ssr.update(camera_main); };
		futureUpdates.push_back(std::async(std::launch::async, updateSSR));

		// TAA
		auto updateTAA = [&]() { taa.update(camera_main); };
		futureUpdates.push_back(std::async(std::launch::async, updateTAA));

		// MOTION BLUR
		auto updateMotionBlur = [&]() { motionBlur.update(camera_main); };
		futureUpdates.push_back(std::async(std::launch::async, updateMotionBlur));

		// SHADOWS
		auto updateShadows = [&]() { shadows.update(camera_main); };
		futureUpdates.push_back(std::async(std::launch::async, updateShadows));

		// COMPOSITION UNIFORMS
		auto updateDeferred = [&]() { deferred.update(camera_main.invViewProjection); };
		futureUpdates.push_back(std::async(std::launch::async, updateDeferred));

		for (auto& f : futureUpdates)
			f.get();

		static Timer timerFenceWait;
		timerFenceWait.Start();
		VulkanContext::get()->waitFences((*VulkanContext::get()->fences)[previousImageIndex]);
		FrameTimer::Instance().timestamps[0] = timerFenceWait.Count();
		Queue::exec_memcpyRequests();

		GUI::updatesTimeCount = static_cast<float>(timer.Count());
	}

	void Renderer::RecordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
	{
		//auto beginInfo = vk::CommandBufferBeginInfo()
		//	.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		//	.setPInheritanceInfo(nullptr);
		//
		//auto& cmd = VulkanContext::get()->computeCmdBuffer;
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

	void Renderer::RecordDeferredCmds(const uint32_t& imageIndex)
	{
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

		const auto& cmd = (*VulkanContext::get()->dynamicCmdBuffers)[imageIndex];

		cmd.begin(beginInfo);
		// TODO: add more queries (times the swapchain images), so they are not overlapped from previous frame
		metrics[0].start(&cmd);

		// SKYBOX
		SkyBox& skybox = GUI::shadow_cast ? skyBoxDay : skyBoxNight;

		// MODELS
		{
			metrics[2].start(&cmd);
			deferred.batchStart(cmd, imageIndex, *renderTargets["viewport"].extent);

			for (auto& model : Model::models)
				model.draw((uint16_t)RenderQueue::Opaque);

			for (auto& model : Model::models)
				model.draw((uint16_t)RenderQueue::AlphaCut);

			for (auto& model : Model::models)
				model.draw((uint16_t)RenderQueue::AlphaBlend);

			deferred.batchEnd();
			metrics[2].end(&GUI::metrics[2]);
		}

		renderTargets["albedo"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["depth"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["normal"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["srm"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["emissive"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["velocity"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["taa"].changeLayout(cmd, LayoutState::ColorRead);
		for (auto& image : shadows.textures)
			image.changeLayout(cmd, LayoutState::DepthRead);

		// SCREEN SPACE AMBIENT OCCLUSION
		if (GUI::show_ssao) {
			metrics[3].start(&cmd);
			renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorWrite);
			ssao.draw(cmd, imageIndex, renderTargets["ssao"]);
			renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorRead);
			metrics[3].end(&GUI::metrics[3]);
		}

		// SCREEN SPACE REFLECTIONS
		if (GUI::show_ssr) {
			metrics[4].start(&cmd);
			renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorWrite);
			ssr.draw(cmd, imageIndex, *renderTargets["ssr"].extent);
			renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorRead);
			metrics[4].end(&GUI::metrics[4]);
		}

		// COMPOSITION
		metrics[5].start(&cmd);
		deferred.draw(cmd, imageIndex, shadows, skybox, *renderTargets["viewport"].extent);
		metrics[5].end(&GUI::metrics[5]);

		if (GUI::use_AntiAliasing) {
			// TAA
			if (GUI::use_TAA) {
				metrics[6].start(&cmd);
				taa.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
				taa.draw(cmd, imageIndex, renderTargets);
				metrics[6].end(&GUI::metrics[6]);
			}
			// FXAA
			else if (GUI::use_FXAA) {
				metrics[6].start(&cmd);
				fxaa.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
				fxaa.draw(cmd, imageIndex, *renderTargets["viewport"].extent);
				metrics[6].end(&GUI::metrics[6]);
			}
		}

		// BLOOM
		if (GUI::show_Bloom) {
			metrics[7].start(&cmd);
			bloom.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
			bloom.draw(cmd, imageIndex, renderTargets);
			metrics[7].end(&GUI::metrics[7]);
		}

		// Depth of Field
		if (GUI::use_DOF) {
			metrics[8].start(&cmd);
			dof.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
			dof.draw(cmd, imageIndex, renderTargets);
			metrics[8].end(&GUI::metrics[8]);
		}

		// MOTION BLUR
		if (GUI::show_motionBlur) {
			metrics[9].start(&cmd);
			motionBlur.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
			motionBlur.draw(cmd, imageIndex, *renderTargets["viewport"].extent);
			metrics[9].end(&GUI::metrics[9]);
		}

		renderTargets["albedo"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["depth"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["normal"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["srm"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["emissive"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["velocity"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["taa"].changeLayout(cmd, LayoutState::ColorWrite);
		for (auto& image : shadows.textures)
			image.changeLayout(cmd, LayoutState::DepthWrite);

		// GUI
		metrics[10].start(&cmd);
		gui.scaleToRenderArea(cmd, renderTargets["viewport"], imageIndex);
		gui.draw(cmd, imageIndex);
		metrics[10].end(&GUI::metrics[10]);

		metrics[0].end(&GUI::metrics[0]);

		cmd.end();
	}

	void Renderer::RecordShadowsCmds(const uint32_t& imageIndex)
	{
		// Render Pass (shadows mapping) (outputs the depth image with the light POV)

		const vk::DeviceSize offset = vk::DeviceSize();
		std::array<vk::ClearValue, 1> clearValuesShadows{};
		clearValuesShadows[0].depthStencil = vk::ClearDepthStencilValue{ 0.0f, 0 };

		vk::CommandBufferBeginInfo beginInfoShadows;
		beginInfoShadows.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

		vk::RenderPassBeginInfo renderPassInfoShadows;
		renderPassInfoShadows.renderPass = *shadows.renderPass.handle;
		renderPassInfoShadows.renderArea = vk::Rect2D{ { 0, 0 },{ Shadows::imageSize, Shadows::imageSize } };
		renderPassInfoShadows.clearValueCount = static_cast<uint32_t>(clearValuesShadows.size());
		renderPassInfoShadows.pClearValues = clearValuesShadows.data();

		for (uint32_t i = 0; i < shadows.textures.size(); i++) {
			auto& cmd = (*VulkanContext::get()->shadowCmdBuffers)[static_cast<uint32_t>(shadows.textures.size()) * imageIndex + i];
			cmd.begin(beginInfoShadows);
			metrics[11 + static_cast<size_t>(i)].start(&cmd);
			cmd.setDepthBias(GUI::depthBias[0], GUI::depthBias[1], GUI::depthBias[2]);

			// depth[i] image ===========================================================
			renderPassInfoShadows.framebuffer = *shadows.framebuffers[shadows.textures.size() * imageIndex + i].handle;
			cmd.beginRenderPass(renderPassInfoShadows, vk::SubpassContents::eInline);
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shadows.pipeline.handle);
			for (auto& model : Model::models) {
				if (model.render) {
					cmd.bindVertexBuffers(0, *model.vertexBuffer.buffer, offset);
					cmd.bindIndexBuffer(*model.indexBuffer.buffer, 0, vk::IndexType::eUint32);

					for (auto& node : model.linearNodes) {
						if (node->mesh) {
							cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *shadows.pipeline.layout, 0, { (*shadows.descriptorSets)[i], *node->mesh->descriptorSet, *model.descriptorSet }, nullptr);
							for (auto& primitive : node->mesh->primitives) {
								if (primitive.render)
									cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
							}
						}
					}
				}
			}
			cmd.endRenderPass();
			metrics[11 + static_cast<size_t>(i)].end(&GUI::metrics[11 + static_cast<size_t>(i)]);
			// ==========================================================================
			cmd.end();
		}
	}

	void Renderer::Destroy()
	{
		for (auto& rt : renderTargets)
			rt.second.destroy();
	}

	void Renderer::Draw()
	{
		auto& vCtx = *VulkanContext::get();

		static const vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader };

		//FIRE_EVENT(Event::OnRender);

		if (GUI::use_compute) {
			//recordComputeCmds(2, 2, 1);
			//vk::SubmitInfo siCompute;
			//siCompute.commandBufferCount = 1;
			//siCompute.setPCommandBuffers = &VulkanContext::get()->computeCmdBuffer;
			//VulkanContext::get()->computeQueue.submit(siCompute, VulkanContext::get()->fences[1]);
			//VulkanContext::get()->device.waitForFences(VulkanContext::get()->fences[1], VK_TRUE, UINT64_MAX);
			//VulkanContext::get()->device.resetFences(VulkanContext::get()->fences[1]);
		}

		// aquire the image
		auto& aquireSignalSemaphore = (*vCtx.semaphores)[0];
		const uint32_t imageIndex = vCtx.swapchain.Aquire(aquireSignalSemaphore, nullptr);
		this->previousImageIndex = imageIndex;

		//static Timer timer;
		//timer.Start();
		//vCtx.waitFences(vCtx.fences[imageIndex]);
		//FrameTimer::Instance().timestamps[0] = timer.Count();

		const auto& cmd = (*vCtx.dynamicCmdBuffers)[imageIndex];

		vCtx.waitAndLockSubmits();

		if (GUI::shadow_cast) {

			// record the shadow command buffers
			RecordShadowsCmds(imageIndex);

			// submit the shadow command buffers
			const auto& shadowWaitSemaphore = aquireSignalSemaphore;
			const auto& shadowSignalSemaphore = (*vCtx.semaphores)[imageIndex * 3 + 1];
			const auto& scb = vCtx.shadowCmdBuffers;
			const auto size = shadows.textures.size();
			const auto i = size * imageIndex;
			const std::vector<vk::CommandBuffer> activeShadowCmdBuffers(scb->begin() + i, scb->begin() + i + size);
			vCtx.submit(activeShadowCmdBuffers, waitStages[0], shadowWaitSemaphore, shadowSignalSemaphore, nullptr);

			aquireSignalSemaphore = shadowSignalSemaphore;
		}

		// record the command buffers
		RecordDeferredCmds(imageIndex);

		// submit the command buffers
		const auto& deferredWaitStage = GUI::shadow_cast ? waitStages[1] : waitStages[0];
		const auto& deferredWaitSemaphore = aquireSignalSemaphore;
		const auto& deferredSignalSemaphore = (*vCtx.semaphores)[imageIndex * 3 + 2];
		const auto& deferredSignalFence = (*vCtx.fences)[imageIndex];
		vCtx.submit(cmd, deferredWaitStage, deferredWaitSemaphore, deferredSignalSemaphore, deferredSignalFence);

		// Presentation
		const auto& presentWaitSemaphore = deferredSignalSemaphore;
		vCtx.swapchain.Present(imageIndex, presentWaitSemaphore, nullptr);

		vCtx.unlockSubmits();
	}

	void Renderer::AddRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags)
	{
		if (renderTargets.find(name) != renderTargets.end())
			return;

		renderTargets[name] = Image();
		renderTargets[name].format = make_ref(format);
		renderTargets[name].initialLayout = make_ref(vk::ImageLayout::eUndefined);
		renderTargets[name].createImage(
			static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
			static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | additionalFlags,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		renderTargets[name].transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
		renderTargets[name].createImageView(vk::ImageAspectFlagBits::eColor);
		renderTargets[name].createSampler();

		//std::string str = to_string(format); str.find("A8") != std::string::npos
		renderTargets[name].blentAttachment->blendEnable = name == "albedo" ? VK_TRUE : VK_FALSE;
		renderTargets[name].blentAttachment->srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		renderTargets[name].blentAttachment->dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		renderTargets[name].blentAttachment->colorBlendOp = vk::BlendOp::eAdd;
		renderTargets[name].blentAttachment->srcAlphaBlendFactor = vk::BlendFactor::eOne;
		renderTargets[name].blentAttachment->dstAlphaBlendFactor = vk::BlendFactor::eZero;
		renderTargets[name].blentAttachment->alphaBlendOp = vk::BlendOp::eAdd;
		renderTargets[name].blentAttachment->colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	}

#ifndef IGNORE_SCRIPTS
	// Callbacks for scripts -------------------
	static void LoadModel(MonoString* folderPath, MonoString* modelName, uint32_t instances)
	{
		const std::string curPath = std::filesystem::current_path().string() + "\\";
		const std::string path(mono_string_to_utf8(folderPath));
		const std::string name(mono_string_to_utf8(modelName));
		for (; instances > 0; instances--)
			Queue::loadModel.emplace_back(curPath + path, name);
	}

	static bool KeyDown(uint32_t key)
	{
		return ImGui::GetIO().KeysDown[key];
	}

	static bool MouseButtonDown(uint32_t button)
	{
		return ImGui::GetIO().MouseDown[button];
	}

	static ImVec2 GetMousePos()
	{
		return ImGui::GetIO().MousePos;
	}

	static void SetMousePos(float x, float y)
	{
		SDL_WarpMouseInWindow(GUI::g_Window, static_cast<int>(x), static_cast<int>(y));
	}

	static float GetMouseWheel()
	{
		return ImGui::GetIO().MouseWheel;
	}

	static void SetTimeScale(float timeScale)
	{
		GUI::timeScale = timeScale;
	}
	// ----------------------------------------
#endif

	void Renderer::LoadResources()
	{
		// SKYBOXES LOAD
		std::array<std::string, 6> skyTextures = {
			"objects/sky/right.png",
			"objects/sky/left.png",
			"objects/sky/top.png",
			"objects/sky/bottom.png",
			"objects/sky/back.png",
			"objects/sky/front.png" };
		skyBoxDay.loadSkyBox(skyTextures, 1024);
		skyTextures = {
			"objects/lmcity/lmcity_rt.png",
			"objects/lmcity/lmcity_lf.png",
			"objects/lmcity/lmcity_up.png",
			"objects/lmcity/lmcity_dn.png",
			"objects/lmcity/lmcity_bk.png",
			"objects/lmcity/lmcity_ft.png" };
		skyBoxNight.loadSkyBox(skyTextures, 512);

		// GUI LOAD
		gui.loadGUI();

#ifndef IGNORE_SCRIPTS
		// SCRIPTS
		Script::Init();
		Script::addCallback("Global::LoadModel", reinterpret_cast<const void*>(LoadModel));
		Script::addCallback("Global::KeyDown", reinterpret_cast<const void*>(KeyDown));
		Script::addCallback("Global::SetTimeScale", reinterpret_cast<const void*>(SetTimeScale));
		Script::addCallback("Global::MouseButtonDown", reinterpret_cast<const void*>(MouseButtonDown));
		Script::addCallback("Global::GetMousePos", reinterpret_cast<const void*>(GetMousePos));
		Script::addCallback("Global::SetMousePos", reinterpret_cast<const void*>(SetMousePos));
		Script::addCallback("Global::GetMouseWheel", reinterpret_cast<const void*>(GetMouseWheel));
		scripts.push_back(new Script("Load"));
#endif
	}

	void Renderer::CreateUniforms()
	{
		// DESCRIPTOR SETS FOR GUI
		gui.createDescriptorSet(GUI::getDescriptorSetLayout(*VulkanContext::get()->device));
		// DESCRIPTOR SETS FOR SKYBOX
		skyBoxDay.createDescriptorSet();
		skyBoxNight.createDescriptorSet();
		// DESCRIPTOR SETS FOR SHADOWS
		shadows.createUniformBuffers();
		shadows.createDescriptorSets();
		// DESCRIPTOR SETS FOR LIGHTS
		lightUniforms.createLightUniforms();
		// DESCRIPTOR SETS FOR SSAO
		ssao.createUniforms(renderTargets);
		// DESCRIPTOR SETS FOR COMPOSITION PIPELINE
		deferred.createDeferredUniforms(renderTargets, lightUniforms);
		// DESCRIPTOR SET FOR REFLECTION PIPELINE
		ssr.createSSRUniforms(renderTargets);
		// DESCRIPTOR SET FOR FXAA PIPELINE
		fxaa.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR TAA PIPELINE
		taa.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR BLOOM PIPELINES
		bloom.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR DEPTH OF FIELD PIPELINE
		dof.createUniforms(renderTargets);
		// DESCRIPTOR SET FOR MOTIONBLUR PIPELINE
		motionBlur.createMotionBlurUniforms(renderTargets);
		// DESCRIPTOR SET FOR COMPUTE PIPELINE
		//compute.createComputeUniforms(sizeof(SBOIn));
	}

	void Renderer::ResizeViewport(uint32_t width, uint32_t height)
	{
		auto& vulkan = *VulkanContext::get();
		vulkan.graphicsQueue->waitIdle();

		//- Free resources ----------------------
		// render targets
		for (auto& RT : renderTargets)
			RT.second.destroy();
		renderTargets.clear();

		// GUI
		gui.renderPass.Destroy();
		for (auto& framebuffer : gui.framebuffers)
			framebuffer.Destroy();
		gui.pipeline.destroy();

		// deferred
		deferred.renderPass.Destroy();
		deferred.compositionRenderPass.Destroy();
		for (auto& framebuffer : deferred.framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : deferred.compositionFramebuffers)
			framebuffer.Destroy();
		deferred.pipeline.destroy();
		deferred.pipelineComposition.destroy();

		// SSR
		for (auto& framebuffer : ssr.framebuffers)
			framebuffer.Destroy();
		ssr.renderPass.Destroy();
		ssr.pipeline.destroy();

		// FXAA
		for (auto& framebuffer : fxaa.framebuffers)
			framebuffer.Destroy();
		fxaa.renderPass.Destroy();
		fxaa.pipeline.destroy();
		fxaa.frameImage.destroy();

		// TAA
		taa.previous.destroy();
		taa.frameImage.destroy();
		for (auto& framebuffer : taa.framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : taa.framebuffersSharpen)
			framebuffer.Destroy();
		taa.renderPass.Destroy();
		taa.renderPassSharpen.Destroy();
		taa.pipeline.destroy();
		taa.pipelineSharpen.destroy();

		// Bloom
		for (auto& frameBuffer : bloom.framebuffers)
			frameBuffer.Destroy();
		bloom.renderPassBrightFilter.Destroy();
		bloom.renderPassGaussianBlur.Destroy();
		bloom.renderPassCombine.Destroy();
		bloom.pipelineBrightFilter.destroy();
		bloom.pipelineGaussianBlurHorizontal.destroy();
		bloom.pipelineGaussianBlurVertical.destroy();
		bloom.pipelineCombine.destroy();
		bloom.frameImage.destroy();

		// Depth of Field
		for (auto& framebuffer : dof.framebuffers)
			framebuffer.Destroy();
		dof.renderPass.Destroy();
		dof.pipeline.destroy();
		dof.frameImage.destroy();

		// Motion blur
		for (auto& framebuffer : motionBlur.framebuffers)
			framebuffer.Destroy();
		motionBlur.renderPass.Destroy();
		motionBlur.pipeline.destroy();
		motionBlur.frameImage.destroy();

		// SSAO
		ssao.renderPass.Destroy();
		ssao.blurRenderPass.Destroy();
		for (auto& framebuffer : ssao.framebuffers)
			framebuffer.Destroy();
		for (auto& framebuffer : ssao.blurFramebuffers)
			framebuffer.Destroy();
		ssao.pipeline.destroy();
		ssao.pipelineBlur.destroy();

		vulkan.depth.destroy();
		vulkan.swapchain.Destroy();
		//- Free resources end ------------------

		//- Recreate resources ------------------
		WIDTH = width;
		HEIGHT = height;
		vulkan.CreateSwapchain(ctx, 3);
		vulkan.CreateDepth();

		AddRenderTarget("viewport", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);
		AddRenderTarget("depth", vk::Format::eR32Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("albedo", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("srm", vulkan.surface.formatKHR->format, vk::ImageUsageFlags()); // Specular Roughness Metallic
		AddRenderTarget("ssao", vk::Format::eR16Unorm, vk::ImageUsageFlags());
		AddRenderTarget("ssaoBlur", vk::Format::eR8Unorm, vk::ImageUsageFlags());
		AddRenderTarget("ssr", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("velocity", vk::Format::eR16G16Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("brightFilter", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("gaussianBlurHorizontal", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("gaussianBlurVertical", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("emissive", vulkan.surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("taa", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);

		deferred.createRenderPasses(renderTargets);
		deferred.createFrameBuffers(renderTargets);
		deferred.createPipelines(renderTargets);
		deferred.updateDescriptorSets(renderTargets, lightUniforms);

		ssr.createRenderPass(renderTargets);
		ssr.createFrameBuffers(renderTargets);
		ssr.createPipeline(renderTargets);
		ssr.updateDescriptorSets(renderTargets);

		fxaa.Init();
		fxaa.createRenderPass(renderTargets);
		fxaa.createFrameBuffers(renderTargets);
		fxaa.createPipeline(renderTargets);
		fxaa.updateDescriptorSets(renderTargets);

		taa.Init();
		taa.createRenderPasses(renderTargets);
		taa.createFrameBuffers(renderTargets);
		taa.createPipelines(renderTargets);
		taa.updateDescriptorSets(renderTargets);

		bloom.Init();
		bloom.createRenderPasses(renderTargets);
		bloom.createFrameBuffers(renderTargets);
		bloom.createPipelines(renderTargets);
		bloom.updateDescriptorSets(renderTargets);

		dof.Init();
		dof.createRenderPass(renderTargets);
		dof.createFrameBuffers(renderTargets);
		dof.createPipeline(renderTargets);
		dof.updateDescriptorSets(renderTargets);

		motionBlur.Init();
		motionBlur.createRenderPass(renderTargets);
		motionBlur.createFrameBuffers(renderTargets);
		motionBlur.createPipeline(renderTargets);
		motionBlur.updateDescriptorSets(renderTargets);

		ssao.createRenderPasses(renderTargets);
		ssao.createFrameBuffers(renderTargets);
		ssao.createPipelines(renderTargets);
		ssao.updateDescriptorSets(renderTargets);

		gui.createRenderPass();
		gui.createFrameBuffers();
		gui.createPipeline();
		gui.updateDescriptorSets();

		//compute.pipeline = createComputePipeline();
		//compute.updateDescriptorSets();
		//- Recreate resources end --------------
	}

	void Renderer::RecreatePipelines()
	{
		VulkanContext::get()->graphicsQueue->waitIdle();

		shadows.pipeline.destroy();
		ssao.pipeline.destroy();
		ssao.pipelineBlur.destroy();
		ssr.pipeline.destroy();
		deferred.pipeline.destroy();
		deferred.pipelineComposition.destroy();
		fxaa.pipeline.destroy();
		taa.pipeline.destroy();
		taa.pipelineSharpen.destroy();
		//bloom.pipelineBrightFilter.destroy();
		//bloom.pipelineCombine.destroy();
		//bloom.pipelineGaussianBlurHorizontal.destroy();
		//bloom.pipelineGaussianBlurVertical.destroy();
		dof.pipeline.destroy();
		motionBlur.pipeline.destroy();
		gui.pipeline.destroy();

		shadows.createPipeline();
		ssao.createPipelines(renderTargets);
		ssr.createPipeline(renderTargets);
		deferred.createPipelines(renderTargets);
		fxaa.createPipeline(renderTargets);
		taa.createPipelines(renderTargets);
		bloom.createPipelines(renderTargets);
		dof.createPipeline(renderTargets);
		motionBlur.createPipeline(renderTargets);
		gui.createPipeline();
	}
}
