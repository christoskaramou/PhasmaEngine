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

#if PE_VULKAN
#include "Renderer/Renderer.h"
#include "Core/Queue.h"
#include "Model/Mesh.h"
#include "Renderer/RHI.h"
#include "Systems/CameraSystem.h"
#include "ECS/Context.h"
#include "Core/Path.h"
#include "Core/Settings.h"
#include "Systems/EventSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Renderer/Command.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/FrameBuffer.h"

namespace pe
{
	Renderer::Renderer()
	{
	}
	
	Renderer::~Renderer()
	{
		RHII.Destroy();
		RHII.Remove();
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

	enum class RenderQueue
	{
		Opaque = 1,
		AlphaCut = 2,
		AlphaBlend = 3
	};

	void Renderer::RecordDeferredCmds(uint32_t imageIndex)
	{
		static GPUTimer gpuTimer[12]{};
		
		FrameTimer& frameTimer = FrameTimer::Instance();

		CommandBuffer* cmd = RHII.dynamicCmdBuffers[imageIndex];
		cmd->Begin();

		gpuTimer[0].Start(cmd);
		// SKYBOX
		SkyBox& skybox = GUI::shadow_cast ? skyBoxDay : skyBoxNight;
		
		// MODELS
		{
			gpuTimer[1].Start(cmd);
			deferred.batchStart(cmd, imageIndex);
			
			for (auto& model : Model::models)
				model.draw((uint16_t) RenderQueue::Opaque);
			
			for (auto& model : Model::models)
				model.draw((uint16_t) RenderQueue::AlphaCut);
			
			for (auto& model : Model::models)
				model.draw((uint16_t) RenderQueue::AlphaBlend);
			
			deferred.batchEnd();
			frameTimer.timestamps[4] = gpuTimer[1].End();
		}
		renderTargets["albedo"]->ChangeLayout(cmd, LayoutState::ColorRead);
		RHII.depth->ChangeLayout(cmd, LayoutState::DepthRead);
		renderTargets["normal"]->ChangeLayout(cmd, LayoutState::ColorRead);
		renderTargets["srm"]->ChangeLayout(cmd, LayoutState::ColorRead);
		renderTargets["emissive"]->ChangeLayout(cmd, LayoutState::ColorRead);
		renderTargets["ssr"]->ChangeLayout(cmd, LayoutState::ColorRead);
		renderTargets["ssaoBlur"]->ChangeLayout(cmd, LayoutState::ColorRead);
		renderTargets["velocity"]->ChangeLayout(cmd, LayoutState::ColorRead);
		renderTargets["taa"]->ChangeLayout(cmd, LayoutState::ColorRead);
		for (auto& image : shadows.textures)
			image->ChangeLayout(cmd, LayoutState::DepthRead);
		
		SSAO& ssao = *WORLD_ENTITY->GetComponent<SSAO>();
		SSR& ssr = *WORLD_ENTITY->GetComponent<SSR>();
		FXAA& fxaa = *WORLD_ENTITY->GetComponent<FXAA>();
		TAA& taa = *WORLD_ENTITY->GetComponent<TAA>();
		Bloom& bloom = *WORLD_ENTITY->GetComponent<Bloom>();
		DOF& dof = *WORLD_ENTITY->GetComponent<DOF>();
		MotionBlur& motionBlur = *WORLD_ENTITY->GetComponent<MotionBlur>();

		// SCREEN SPACE AMBIENT OCCLUSION
		if (GUI::show_ssao)
		{
			gpuTimer[2].Start(cmd);
			renderTargets["ssaoBlur"]->ChangeLayout(cmd, LayoutState::ColorWrite);
			ssao.draw(cmd, imageIndex, renderTargets["ssao"]);
			renderTargets["ssaoBlur"]->ChangeLayout(cmd, LayoutState::ColorRead);
			frameTimer.timestamps[5] = gpuTimer[2].End();
		}
		
		// SCREEN SPACE REFLECTIONS
		if (GUI::show_ssr)
		{
			gpuTimer[3].Start(cmd);
			renderTargets["ssr"]->ChangeLayout(cmd, LayoutState::ColorWrite);
			ssr.draw(cmd, imageIndex);
			renderTargets["ssr"]->ChangeLayout(cmd, LayoutState::ColorRead);
			frameTimer.timestamps[6] = gpuTimer[3].End();
		}
		
		// COMPOSITION
		gpuTimer[4].Start(cmd);
		deferred.draw(cmd, imageIndex, shadows, skybox);
		frameTimer.timestamps[7] = gpuTimer[4].End();
		
		if (GUI::use_AntiAliasing)
		{
			// TAA
			if (GUI::use_TAA)
			{
				gpuTimer[5].Start(cmd);
				taa.frameImage->CopyColorAttachment(cmd, renderTargets["viewport"]);
				taa.draw(cmd, imageIndex, renderTargets);
				frameTimer.timestamps[8] = gpuTimer[5].End();
			}
				// FXAA
			else if (GUI::use_FXAA)
			{
				gpuTimer[6].Start(cmd);
				fxaa.frameImage->CopyColorAttachment(cmd, renderTargets["viewport"]);
				fxaa.draw(cmd, imageIndex);
				frameTimer.timestamps[8] = gpuTimer[6].End();
			}
		}
		
		// BLOOM
		if (GUI::show_Bloom)
		{
			gpuTimer[7].Start(cmd);
			bloom.frameImage->CopyColorAttachment(cmd, renderTargets["viewport"]);
			bloom.draw(cmd, imageIndex, renderTargets);
			frameTimer.timestamps[9] = gpuTimer[7].End();
		}
		
		// Depth of Field
		if (GUI::use_DOF)
		{
			gpuTimer[8].Start(cmd);
			dof.frameImage->CopyColorAttachment(cmd, renderTargets["viewport"]);
			dof.draw(cmd, imageIndex, renderTargets);
			frameTimer.timestamps[10] = gpuTimer[8].End();
		}
		
		// MOTION BLUR
		if (GUI::show_motionBlur)
		{
			gpuTimer[9].Start(cmd);
			motionBlur.frameImage->CopyColorAttachment(cmd, renderTargets["viewport"]);
			motionBlur.draw(cmd, imageIndex);
			frameTimer.timestamps[11] = gpuTimer[9].End();
		}

		renderTargets["albedo"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		RHII.depth->ChangeLayout(cmd, LayoutState::DepthWrite);
		renderTargets["normal"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["srm"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["emissive"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["ssr"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["ssaoBlur"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["velocity"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		renderTargets["taa"]->ChangeLayout(cmd, LayoutState::ColorWrite);
		for (auto& image : shadows.textures)
			image->ChangeLayout(cmd, LayoutState::DepthWrite);

		BlitToViewport(cmd, GUI::s_currRenderImage ? GUI::s_currRenderImage : renderTargets["viewport"], imageIndex);

		// GUI
		gpuTimer[10].Start(cmd);
		gui.Draw(cmd, imageIndex);
		frameTimer.timestamps[12] = gpuTimer[10].End();
		
		frameTimer.timestamps[2] = gpuTimer[0].End();
		
		cmd->End();
	}
	
	void Renderer::RecordShadowsCmds(uint32_t imageIndex)
	{
		static GPUTimer gpuTimer[SHADOWMAP_CASCADES]{};
		
		for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
		{
			uint32_t index = SHADOWMAP_CASCADES * imageIndex + i;
			CommandBuffer& cmd = *RHII.shadowCmdBuffers[index];
			cmd.Begin();

			FrameTimer& frameTimer = FrameTimer::Instance();
			gpuTimer[i].Start(&cmd);

			cmd.SetDepthBias(GUI::depthBias[0], GUI::depthBias[1], GUI::depthBias[2]);
			cmd.BeginPass(shadows.renderPass, shadows.framebuffers[index]);
			cmd.BindPipeline(&shadows.pipeline);
			for (auto& model : Model::models)
			{
				if (model.render)
				{
					cmd.BindVertexBuffer(model.vertexBuffer, 0);
					cmd.BindIndexBuffer(model.indexBuffer, 0);
					
					for (auto& node : model.linearNodes)
					{
						if (node->mesh)
						{
							std::array<Descriptor*, 2> descriptors{ node->mesh->descriptorSet, model.descriptorSet };
							cmd.PushConstants(&shadows.pipeline, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &shadows.cascades[i]);
							cmd.BindDescriptors(&shadows.pipeline, 2, descriptors.data());
							for (auto& primitive : node->mesh->primitives)
							{
								//if (primitive.render)
									cmd.DrawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset,
										node->mesh->vertexOffset + primitive.vertexOffset, 0);
							}
						}
					}
				}
			}
			cmd.EndPass();

			frameTimer.timestamps[13 + static_cast<size_t>(i)] = gpuTimer[i].End();

			cmd.End();
		}
	}
	
	void Renderer::AddRenderTarget(const std::string& name, Format format, ImageUsageFlags additionalFlags)
	{
		if (renderTargets.find(name) != renderTargets.end())
			return;
		
		ImageCreateInfo info{};
		info.format = format;
		info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
		info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
		info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | additionalFlags;
		info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		renderTargets[name] = Image::Create(info);
		renderTargets[name]->name = name;

		ImageViewCreateInfo viewInfo{};
		viewInfo.image = renderTargets[name];
		renderTargets[name]->CreateImageView(viewInfo);

		SamplerCreateInfo samplerInfo{};
		renderTargets[name]->CreateSampler(samplerInfo);

		renderTargets[name]->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		renderTargets[name]->imageInfo.layoutState = LayoutState::ColorWrite;
		
		//std::string str = to_string(format); str.find("A8") != std::string::npos
		renderTargets[name]->blendAttachment.blendEnable = name == "albedo" ? VK_TRUE : VK_FALSE;
		renderTargets[name]->blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		renderTargets[name]->blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		renderTargets[name]->blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		renderTargets[name]->blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		renderTargets[name]->blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		renderTargets[name]->blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		renderTargets[name]->blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		GUI::s_renderImages.push_back(renderTargets[name]);
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
		RHII.WaitGraphicsQueue();

		renderArea.Update(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

		SSAO& ssao = *WORLD_ENTITY->GetComponent<SSAO>();
		SSR& ssr = *WORLD_ENTITY->GetComponent<SSR>();
		FXAA& fxaa = *WORLD_ENTITY->GetComponent<FXAA>();
		TAA& taa = *WORLD_ENTITY->GetComponent<TAA>();
		Bloom& bloom = *WORLD_ENTITY->GetComponent<Bloom>();
		DOF& dof = *WORLD_ENTITY->GetComponent<DOF>();
		MotionBlur& motionBlur = *WORLD_ENTITY->GetComponent<MotionBlur>();

		//- Free resources ----------------------
		// render targets
		for (auto& RT : renderTargets)
			RT.second->Destroy();
		renderTargets.clear();
		GUI::s_renderImages.clear();
		
		// GUI
		gui.renderPass->Destroy();
		
		// deferred
		deferred.renderPass->Destroy();
		deferred.compositionRenderPass->Destroy();
		for (auto* framebuffer : deferred.framebuffers)
			framebuffer->Destroy();
		for (auto* framebuffer : deferred.compositionFramebuffers)
			framebuffer->Destroy();
		deferred.pipeline.destroy();
		deferred.pipelineComposition.destroy();
		
		// SSR
		for (auto framebuffer : ssr.framebuffers)
			framebuffer->Destroy();
		ssr.renderPass->Destroy();
		ssr.pipeline.destroy();
		
		// FXAA
		for (auto& framebuffer : fxaa.framebuffers)
			framebuffer->Destroy();
		fxaa.renderPass->Destroy();
		fxaa.pipeline.destroy();
		fxaa.frameImage->Destroy();
		
		// TAA
		taa.previous->Destroy();
		taa.frameImage->Destroy();
		for (auto& framebuffer : taa.framebuffers)
			framebuffer->Destroy();
		for (auto& framebuffer : taa.framebuffersSharpen)
			framebuffer->Destroy();
		taa.renderPass->Destroy();
		taa.renderPassSharpen->Destroy();
		taa.pipeline.destroy();
		taa.pipelineSharpen.destroy();
		
		// Bloom
		for (auto* frameBuffer : bloom.framebuffers)
			frameBuffer->Destroy();
		bloom.renderPassBrightFilter->Destroy();
		bloom.renderPassGaussianBlur->Destroy();
		bloom.renderPassCombine->Destroy();
		bloom.pipelineBrightFilter.destroy();
		bloom.pipelineGaussianBlurHorizontal.destroy();
		bloom.pipelineGaussianBlurVertical.destroy();
		bloom.pipelineCombine.destroy();
		bloom.frameImage->Destroy();
		
		// Depth of Field
		for (auto& framebuffer : dof.framebuffers)
			framebuffer->Destroy();
		dof.renderPass->Destroy();
		dof.pipeline.destroy();
		dof.frameImage->Destroy();
		
		// Motion blur
		for (auto& framebuffer : motionBlur.framebuffers)
			framebuffer->Destroy();
		motionBlur.renderPass->Destroy();
		motionBlur.pipeline.destroy();
		motionBlur.frameImage->Destroy();
		
		// SSAO
		ssao.renderPass->Destroy();
		ssao.blurRenderPass->Destroy();
		for (auto& framebuffer : ssao.framebuffers)
			framebuffer->Destroy();
		for (auto& framebuffer : ssao.blurFramebuffers)
			framebuffer->Destroy();
		ssao.pipeline.destroy();
		ssao.pipelineBlur.destroy();
		
		RHII.depth->Destroy();
		RHII.swapchain->Destroy();
		//- Free resources end ------------------
		
		//- Recreate resources ------------------
		WIDTH = width;
		HEIGHT = height;
		RHII.CreateSwapchain(&RHII.surface);
		RHII.CreateDepth();
		
		AddRenderTarget("viewport", RHII.surface.format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		AddRenderTarget("normal", VK_FORMAT_R32G32B32A32_SFLOAT);
		AddRenderTarget("albedo", RHII.surface.format);
		AddRenderTarget("srm", RHII.surface.format); // Specular Roughness Metallic
		AddRenderTarget("ssao", VK_FORMAT_R16_UNORM);
		AddRenderTarget("ssaoBlur", VK_FORMAT_R8_UNORM);
		AddRenderTarget("ssr", RHII.surface.format);
		AddRenderTarget("velocity", VK_FORMAT_R16G16_SFLOAT);
		AddRenderTarget("brightFilter", RHII.surface.format);
		AddRenderTarget("gaussianBlurHorizontal", RHII.surface.format);
		AddRenderTarget("gaussianBlurVertical", RHII.surface.format);
		AddRenderTarget("emissive", RHII.surface.format);
		AddRenderTarget("taa", RHII.surface.format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		
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
			
	void Renderer::BlitToViewport(CommandBuffer* cmd, Image* renderedImage, uint32_t imageIndex)
	{
		Image* s_chain_Image = RHII.swapchain->images[imageIndex];
		
		renderedImage->TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT
		);
		s_chain_Image->TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT
		);
		
		ImageBlit blit{};
		blit.srcOffsets[0] = Offset3D{ static_cast<int32_t>(renderedImage->imageInfo.width), static_cast<int32_t>(renderedImage->imageInfo.height), 0 };
		blit.srcOffsets[1] = Offset3D{ 0, 0, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		Viewport& vp = renderArea.viewport;
		blit.dstOffsets[0] = Offset3D{ (int32_t)vp.x, (int32_t)vp.y, 0 };
		blit.dstOffsets[1] = Offset3D{ (int32_t)vp.x + (int32_t)vp.width, (int32_t)vp.y + (int32_t)vp.height, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		
		cmd->BlitImage(
			renderedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			s_chain_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);
		
		renderedImage->TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		);
		s_chain_Image->TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
		);
	}
	
	void Renderer::RecreatePipelines()
	{
		RHII.WaitGraphicsQueue();

		SSAO& ssao = *WORLD_ENTITY->GetComponent<SSAO>();
		SSR& ssr = *WORLD_ENTITY->GetComponent<SSR>();
		FXAA& fxaa = *WORLD_ENTITY->GetComponent<FXAA>();
		TAA& taa = *WORLD_ENTITY->GetComponent<TAA>();
		Bloom& bloom = *WORLD_ENTITY->GetComponent<Bloom>();
		DOF& dof = *WORLD_ENTITY->GetComponent<DOF>();
		MotionBlur& motionBlur = *WORLD_ENTITY->GetComponent<MotionBlur>();

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
		
		CONTEXT->GetSystem<CameraSystem>()->GetCamera(0)->ReCreateComputePipelines();
	}
}
#endif
