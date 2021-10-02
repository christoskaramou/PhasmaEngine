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

#include "PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"

namespace pe
{
	PostProcessSystem::PostProcessSystem()
	{
	}

	PostProcessSystem::~PostProcessSystem()
	{
	}

	void PostProcessSystem::Init()
	{
		Bloom& bloom = *Context::Get()->MainEntity->CreateComponent<Bloom>();
		DOF& dof = *Context::Get()->MainEntity->CreateComponent<DOF>();
		FXAA& fxaa = *Context::Get()->MainEntity->CreateComponent<FXAA>();
		MotionBlur& motionBlur = *Context::Get()->MainEntity->CreateComponent<MotionBlur>();
		SSAO& ssao = *Context::Get()->MainEntity->CreateComponent<SSAO>();
		SSR& ssr = *Context::Get()->MainEntity->CreateComponent<SSR>();
		TAA& taa = *Context::Get()->MainEntity->CreateComponent<TAA>();

		bloom.Init();
		dof.Init();
		fxaa.Init();
		motionBlur.Init();
		taa.Init();

		auto& renderTargets = Context::Get()->GetSystem<RendererSystem>()->GetRenderTargets();

		// Render passes
		ssao.createRenderPasses(renderTargets);
		ssr.createRenderPass(renderTargets);
		fxaa.createRenderPass(renderTargets);
		taa.createRenderPasses(renderTargets);
		bloom.createRenderPasses(renderTargets);
		dof.createRenderPass(renderTargets);
		motionBlur.createRenderPass(renderTargets);

		// Frame buffers
		ssao.createFrameBuffers(renderTargets);
		ssr.createFrameBuffers(renderTargets);
		fxaa.createFrameBuffers(renderTargets);
		taa.createFrameBuffers(renderTargets);
		bloom.createFrameBuffers(renderTargets);
		dof.createFrameBuffers(renderTargets);
		motionBlur.createFrameBuffers(renderTargets);

		// Pipelines
		ssao.createPipelines(renderTargets);
		ssr.createPipeline(renderTargets);
		fxaa.createPipeline(renderTargets);
		taa.createPipelines(renderTargets);
		bloom.createPipelines(renderTargets);
		dof.createPipeline(renderTargets);
		motionBlur.createPipeline(renderTargets);

		// Descriptor Sets
		ssao.createUniforms(renderTargets);
		ssr.createSSRUniforms(renderTargets);
		fxaa.createUniforms(renderTargets);
		taa.createUniforms(renderTargets);
		bloom.createUniforms(renderTargets);
		dof.createUniforms(renderTargets);
		motionBlur.createMotionBlurUniforms(renderTargets);
	}

	void PostProcessSystem::Update(double delta)
	{
		SSAO* ssao = Context::Get()->MainEntity->GetComponent<SSAO>();
		SSR* ssr = Context::Get()->MainEntity->GetComponent<SSR>();
		TAA* taa = Context::Get()->MainEntity->GetComponent<TAA>();
		MotionBlur* motionBlur = Context::Get()->MainEntity->GetComponent<MotionBlur>();

		Camera* camera_main = Context::Get()->GetSystem<CameraSystem>()->GetCamera(0);

		auto updateSSAO = [camera_main, ssao]() { ssao->update(*camera_main); };
		auto updateSSR = [camera_main, ssr]() { ssr->update(*camera_main); };
		auto updateTAA = [camera_main, taa]() { taa->update(*camera_main); };
		auto updateMotionBlur = [camera_main, motionBlur]() { motionBlur->update(*camera_main); };

		Queue<Launch::Async>::Request(updateSSAO);
		Queue<Launch::Async>::Request(updateSSR);
		Queue<Launch::Async>::Request(updateTAA);
		Queue<Launch::Async>::Request(updateMotionBlur);
	}

	void PostProcessSystem::Destroy()
	{
		Context::Get()->MainEntity->GetComponent<Bloom>()->destroy();
		Context::Get()->MainEntity->GetComponent<DOF>()->destroy();
		Context::Get()->MainEntity->GetComponent<FXAA>()->destroy();
		Context::Get()->MainEntity->GetComponent<MotionBlur>()->destroy();
		Context::Get()->MainEntity->GetComponent<SSAO>()->destroy();
		Context::Get()->MainEntity->GetComponent<SSR>()->destroy();
		Context::Get()->MainEntity->GetComponent<TAA>()->destroy();
	}
}
