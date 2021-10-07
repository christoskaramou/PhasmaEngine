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
#include "Renderer.h"
#include "Core/Queue.h"
#include "Model/Mesh.h"
#include "Renderer/RenderApi.h"
#include "Systems/CameraSystem.h"
#include "ECS/Context.h"
#include "Core/Path.h"
#include "Systems/EventSystem.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
	Renderer::Renderer()
	{
	}
	
	Renderer::~Renderer()
	{
		Context::Get()->GetVKContext()->Destroy();
		Context::Get()->GetVKContext()->Remove();
	}

	void RenderArea::Update(float x, float y, float w, float h, float minDepth, float maxDepth)
	{
		viewport.x = x;
		viewport.y = y;
		viewport.width = w;
		viewport.height = h;
		viewport.minDepth = minDepth;
		viewport.maxDepth = maxDepth;

		scissor.x = static_cast<int>(x);
		scissor.y = static_cast<int>(y);
		scissor.width = static_cast<int>(w);
		scissor.height = static_cast<int>(h);
	}
	
	void Renderer::CheckQueue()
	{
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
	
	
	void Renderer::ComputeAnimations()
	{
	
	}

	void Renderer::RecordDeferredCmds(const uint32_t& imageIndex)
	{
		static GPUTimer gpuTimer[12]{};

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		
		const auto& cmd = (*VulkanContext::Get()->dynamicCmdBuffers)[imageIndex];
		FrameTimer& frameTimer = FrameTimer::Instance();

		cmd.begin(beginInfo);
		// TODO: add more queries (times the swapchain images), so they are not overlapped from previous frame

		gpuTimer[0].Start(&cmd);
		// SKYBOX
		SkyBox& skybox = GUI::shadow_cast ? skyBoxDay : skyBoxNight;
		
		// MODELS
		{
			gpuTimer[1].Start(&cmd);
			deferred.batchStart(cmd, imageIndex, *renderTargets["viewport"].extent);
			
			for (auto& model : Model::models)
				model.draw((uint16_t) RenderQueue::Opaque);
			
			for (auto& model : Model::models)
				model.draw((uint16_t) RenderQueue::AlphaCut);
			
			for (auto& model : Model::models)
				model.draw((uint16_t) RenderQueue::AlphaBlend);
			
			deferred.batchEnd();
			frameTimer.timestamps[4] = gpuTimer[1].End();
		}
		renderTargets["albedo"].changeLayout(cmd, LayoutState::ColorRead);
		VulkanContext::Get()->depth.changeLayout(cmd, LayoutState::DepthRead);
		renderTargets["normal"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["srm"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["emissive"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["velocity"].changeLayout(cmd, LayoutState::ColorRead);
		renderTargets["taa"].changeLayout(cmd, LayoutState::ColorRead);
		for (auto& image : shadows.textures)
			image.changeLayout(cmd, LayoutState::DepthRead);
		
		SSAO& ssao = *Context::Get()->MainEntity->GetComponent<SSAO>();
		SSR& ssr = *Context::Get()->MainEntity->GetComponent<SSR>();
		FXAA& fxaa = *Context::Get()->MainEntity->GetComponent<FXAA>();
		TAA& taa = *Context::Get()->MainEntity->GetComponent<TAA>();
		Bloom& bloom = *Context::Get()->MainEntity->GetComponent<Bloom>();
		DOF& dof = *Context::Get()->MainEntity->GetComponent<DOF>();
		MotionBlur& motionBlur = *Context::Get()->MainEntity->GetComponent<MotionBlur>();

		// SCREEN SPACE AMBIENT OCCLUSION
		if (GUI::show_ssao)
		{
			gpuTimer[2].Start(&cmd);
			renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorWrite);
			ssao.draw(cmd, imageIndex, renderTargets["ssao"]);
			renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorRead);
			frameTimer.timestamps[5] = gpuTimer[2].End();
		}
		
		// SCREEN SPACE REFLECTIONS
		if (GUI::show_ssr)
		{
			gpuTimer[3].Start(&cmd);
			renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorWrite);
			ssr.draw(cmd, imageIndex, *renderTargets["ssr"].extent);
			renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorRead);
			frameTimer.timestamps[6] = gpuTimer[3].End();
		}
		
		// COMPOSITION
		gpuTimer[4].Start(&cmd);
		deferred.draw(cmd, imageIndex, shadows, skybox, *renderTargets["viewport"].extent);
		frameTimer.timestamps[7] = gpuTimer[4].End();
		
		if (GUI::use_AntiAliasing)
		{
			// TAA
			if (GUI::use_TAA)
			{
				gpuTimer[5].Start(&cmd);
				taa.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
				taa.draw(cmd, imageIndex, renderTargets);
				frameTimer.timestamps[8] = gpuTimer[5].End();
			}
				// FXAA
			else if (GUI::use_FXAA)
			{
				gpuTimer[6].Start(&cmd);
				fxaa.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
				fxaa.draw(cmd, imageIndex, *renderTargets["viewport"].extent);
				frameTimer.timestamps[8] = gpuTimer[6].End();
			}
		}
		
		// BLOOM
		if (GUI::show_Bloom)
		{
			gpuTimer[7].Start(&cmd);
			bloom.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
			bloom.draw(cmd, imageIndex, renderTargets);
			frameTimer.timestamps[9] = gpuTimer[7].End();
		}
		
		// Depth of Field
		if (GUI::use_DOF)
		{
			gpuTimer[8].Start(&cmd);
			dof.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
			dof.draw(cmd, imageIndex, renderTargets);
			frameTimer.timestamps[10] = gpuTimer[8].End();
		}
		
		// MOTION BLUR
		if (GUI::show_motionBlur)
		{
			gpuTimer[9].Start(&cmd);
			motionBlur.frameImage.copyColorAttachment(cmd, renderTargets["viewport"]);
			motionBlur.draw(cmd, imageIndex, *renderTargets["viewport"].extent);
			frameTimer.timestamps[11] = gpuTimer[9].End();
		}

		renderTargets["albedo"].changeLayout(cmd, LayoutState::ColorWrite);
		VulkanContext::Get()->depth.changeLayout(cmd, LayoutState::DepthWrite);
		renderTargets["normal"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["srm"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["emissive"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["ssr"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["ssaoBlur"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["velocity"].changeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["taa"].changeLayout(cmd, LayoutState::ColorWrite);
		for (auto& image : shadows.textures)
			image.changeLayout(cmd, LayoutState::DepthWrite);

		BlitToViewport(cmd, GUI::s_currRenderImage ? *GUI::s_currRenderImage : renderTargets["viewport"], imageIndex);

		// GUI
		gpuTimer[10].Start(&cmd);
		gui.Draw(cmd, imageIndex);
		frameTimer.timestamps[12] = gpuTimer[10].End();
		
		frameTimer.timestamps[2] = gpuTimer[0].End();
		
		cmd.end();
	}
	
	void Renderer::RecordShadowsCmds(const uint32_t& imageIndex)
	{
		// Render Pass (shadows mapping) (outputs the depth image with the light POV)
		
		const vk::DeviceSize offset = vk::DeviceSize();
		std::array<vk::ClearValue, 1> clearValuesShadows {};
		clearValuesShadows[0].depthStencil = vk::ClearDepthStencilValue {0.0f, 0};
		
		vk::CommandBufferBeginInfo beginInfoShadows;
		beginInfoShadows.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		
		vk::RenderPassBeginInfo renderPassInfoShadows;
		renderPassInfoShadows.renderPass = *shadows.renderPass.handle;
		renderPassInfoShadows.renderArea = vk::Rect2D{ {0, 0}, {Shadows::imageSize, Shadows::imageSize} };
		renderPassInfoShadows.clearValueCount = static_cast<uint32_t>(clearValuesShadows.size());
		renderPassInfoShadows.pClearValues = clearValuesShadows.data();

		static GPUTimer gpuTimer[3]{};
		
		for (uint32_t i = 0; i < shadows.textures.size(); i++)
		{
			auto& cmd = (*VulkanContext::Get()->shadowCmdBuffers)[
					static_cast<uint32_t>(shadows.textures.size()) * imageIndex + i];
			cmd.begin(beginInfoShadows);

			FrameTimer& frameTimer = FrameTimer::Instance();
			gpuTimer[i].Start(&cmd);

			cmd.setDepthBias(GUI::depthBias[0], GUI::depthBias[1], GUI::depthBias[2]);

			renderPassInfoShadows.framebuffer = *shadows.framebuffers[shadows.textures.size() * imageIndex + i].handle;
			cmd.beginRenderPass(renderPassInfoShadows, vk::SubpassContents::eInline);
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shadows.pipeline.handle);
			for (auto& model : Model::models)
			{
				if (model.render)
				{
					cmd.bindVertexBuffers(0, *model.vertexBuffer.GetBufferVK(), offset);
					cmd.bindIndexBuffer(*model.indexBuffer.GetBufferVK(), 0, vk::IndexType::eUint32);
					
					for (auto& node : model.linearNodes)
					{
						if (node->mesh)
						{
							cmd.pushConstants<mat4>(*shadows.pipeline.layout, vk::ShaderStageFlagBits::eVertex, 0, shadows.cascades[i]);
							cmd.bindDescriptorSets(
								vk::PipelineBindPoint::eGraphics,
								*shadows.pipeline.layout,
								0,
								{ *node->mesh->descriptorSet, *model.descriptorSet },
								nullptr);

							for (auto& primitive : node->mesh->primitives)
							{
								//if (primitive.render)
									cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset,
											node->mesh->vertexOffset + primitive.vertexOffset, 0);
							}
						}
					}
				}
			}
			cmd.endRenderPass();

			frameTimer.timestamps[13 + static_cast<size_t>(i)] = gpuTimer[i].End();

			cmd.end();
		}
	}
	
	void
	Renderer::AddRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags)
	{
		if (renderTargets.find(name) != renderTargets.end())
			return;
		
		renderTargets[name] = Image();
		renderTargets[name].name = name;
		renderTargets[name].format = make_ref(format);
		renderTargets[name].initialLayout = make_ref(vk::ImageLayout::eUndefined);
		renderTargets[name].createImage(
				static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
				static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
				vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | additionalFlags,
				vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		renderTargets[name]
				.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
		renderTargets[name].createImageView(vk::ImageAspectFlagBits::eColor);
		renderTargets[name].createSampler();
		renderTargets[name].SetDebugName("RenderTarget_" + name);
		
		//std::string str = to_string(format); str.find("A8") != std::string::npos
		renderTargets[name].blentAttachment->blendEnable = name == "albedo" ? VK_TRUE : VK_FALSE;
		renderTargets[name].blentAttachment->srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		renderTargets[name].blentAttachment->dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		renderTargets[name].blentAttachment->colorBlendOp = vk::BlendOp::eAdd;
		renderTargets[name].blentAttachment->srcAlphaBlendFactor = vk::BlendFactor::eOne;
		renderTargets[name].blentAttachment->dstAlphaBlendFactor = vk::BlendFactor::eZero;
		renderTargets[name].blentAttachment->alphaBlendOp = vk::BlendOp::eAdd;
		renderTargets[name].blentAttachment->colorWriteMask =
				vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
				vk::ColorComponentFlagBits::eA;

		GUI::s_renderImages.push_back(&renderTargets[name]);
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
				Path::Assets + "Objects/sky/right.png",
				Path::Assets + "Objects/sky/left.png",
				Path::Assets + "Objects/sky/top.png",
				Path::Assets + "Objects/sky/bottom.png",
				Path::Assets + "Objects/sky/back.png",
				Path::Assets + "Objects/sky/front.png"
		};
		skyBoxDay.loadSkyBox(skyTextures, 1024);
		skyTextures = {
				Path::Assets + "Objects/lmcity/lmcity_rt.png",
				Path::Assets + "Objects/lmcity/lmcity_lf.png",
				Path::Assets + "Objects/lmcity/lmcity_up.png",
				Path::Assets + "Objects/lmcity/lmcity_dn.png",
				Path::Assets + "Objects/lmcity/lmcity_bk.png",
				Path::Assets + "Objects/lmcity/lmcity_ft.png"
		};
		skyBoxNight.loadSkyBox(skyTextures, 512);

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
		// DESCRIPTOR SETS FOR SKYBOX
		skyBoxDay.createDescriptorSet();
		skyBoxNight.createDescriptorSet();
		// DESCRIPTOR SETS FOR SHADOWS
		shadows.createUniformBuffers();
		shadows.createDescriptorSets();
		// DESCRIPTOR SETS FOR COMPOSITION PIPELINE
		deferred.createDeferredUniforms(renderTargets);
		// DESCRIPTOR SET FOR COMPUTE PIPELINE
		//compute.createComputeUniforms(sizeof(SBOIn));
	}
	
	void Renderer::ResizeViewport(uint32_t width, uint32_t height)
	{
		auto& vulkan = *VulkanContext::Get();
		vulkan.graphicsQueue->waitIdle();

		renderArea.Update(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

		SSAO& ssao = *Context::Get()->MainEntity->GetComponent<SSAO>();
		SSR& ssr = *Context::Get()->MainEntity->GetComponent<SSR>();
		FXAA& fxaa = *Context::Get()->MainEntity->GetComponent<FXAA>();
		TAA& taa = *Context::Get()->MainEntity->GetComponent<TAA>();
		Bloom& bloom = *Context::Get()->MainEntity->GetComponent<Bloom>();
		DOF& dof = *Context::Get()->MainEntity->GetComponent<DOF>();
		MotionBlur& motionBlur = *Context::Get()->MainEntity->GetComponent<MotionBlur>();

		//- Free resources ----------------------
		// render targets
		for (auto& RT : renderTargets)
			RT.second.destroy();
		renderTargets.clear();
		GUI::s_renderImages.clear();
		
		// GUI
		gui.renderPass.Destroy();
		
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
		vulkan.CreateSwapchain(&VULKAN.surface);
		vulkan.CreateDepth();
		
		AddRenderTarget("viewport", vulkan.surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);
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
		deferred.updateDescriptorSets(renderTargets);
		
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
		
		gui.CreateRenderPass();
		gui.CreateFrameBuffers();
		
		//compute.pipeline = createComputePipeline();
		//compute.updateDescriptorSets();

		//- Recreate resources end --------------
	}
			
	void Renderer::BlitToViewport(vk::CommandBuffer cmd, Image& renderedImage, uint32_t imageIndex)
	{
		Image& s_chain_Image = VulkanContext::Get()->swapchain.images[imageIndex];
		
		renderedImage.transitionImageLayout(
			cmd,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageAspectFlagBits::eColor
		);
		s_chain_Image.transitionImageLayout(
			cmd,
			vk::ImageLayout::ePresentSrcKHR,
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eColorAttachmentRead,
			vk::AccessFlagBits::eTransferWrite,
			vk::ImageAspectFlagBits::eColor
		);
		
		vk::ImageBlit blit;
		blit.srcOffsets[0] = vk::Offset3D {0, 0, 0};
		blit.srcOffsets[1] = vk::Offset3D{ static_cast<int32_t>(renderedImage.width), static_cast<int32_t>(renderedImage.height), 1 };
		blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.srcSubresource.layerCount = 1;
		Viewport& vp = renderArea.viewport;
		blit.dstOffsets[0] = vk::Offset3D {static_cast<int32_t>(vp.x), static_cast<int32_t>(vp.y), 0};
		blit.dstOffsets[1] = vk::Offset3D {
				static_cast<int32_t>(vp.x) + static_cast<int32_t>(vp.width),
				static_cast<int32_t>(vp.y) + static_cast<int32_t>(vp.height), 1
		};
		blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.dstSubresource.layerCount = 1;
		
		cmd.blitImage(
			*renderedImage.image,
			vk::ImageLayout::eTransferSrcOptimal,
			*s_chain_Image.image,
			vk::ImageLayout::eTransferDstOptimal,
			blit,
			vk::Filter::eLinear
		);
		
		renderedImage.transitionImageLayout(
			cmd,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::AccessFlagBits::eTransferRead,
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::ImageAspectFlagBits::eColor
		);
		s_chain_Image.transitionImageLayout(
			cmd,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::AccessFlagBits::eTransferWrite,
			vk::AccessFlagBits::eColorAttachmentRead,
			vk::ImageAspectFlagBits::eColor
		);
	}
	
	void Renderer::RecreatePipelines()
	{
		VulkanContext::Get()->graphicsQueue->waitIdle();

		SSAO& ssao = *Context::Get()->MainEntity->GetComponent<SSAO>();
		SSR& ssr = *Context::Get()->MainEntity->GetComponent<SSR>();
		FXAA& fxaa = *Context::Get()->MainEntity->GetComponent<FXAA>();
		TAA& taa = *Context::Get()->MainEntity->GetComponent<TAA>();
		Bloom& bloom = *Context::Get()->MainEntity->GetComponent<Bloom>();
		DOF& dof = *Context::Get()->MainEntity->GetComponent<DOF>();
		MotionBlur& motionBlur = *Context::Get()->MainEntity->GetComponent<MotionBlur>();

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
		
		shadows.createPipeline();
		ssao.createPipelines(renderTargets);
		ssr.createPipeline(renderTargets);
		deferred.createPipelines(renderTargets);
		fxaa.createPipeline(renderTargets);
		taa.createPipelines(renderTargets);
		bloom.createPipelines(renderTargets);
		dof.createPipeline(renderTargets);
		motionBlur.createPipeline(renderTargets);
		
		Context::Get()->GetSystem<CameraSystem>()->GetCamera(0)->ReCreateComputePipelines();
	}
}
