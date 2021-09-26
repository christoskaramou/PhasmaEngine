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

#pragma once

#include "Renderer/Image.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Skybox/Skybox.h"
#include "Renderer/Shadows.h"
#include "Model/Model.h"
#include "Renderer/Deferred.h"
#include "Renderer/Compute.h"
#include "Core/Timer.h"
#include "Script/Script.h"
#include "PostProcess/Bloom.h"
#include "PostProcess/DOF.h"
#include "PostProcess/FXAA.h"
#include "PostProcess/MotionBlur.h"
#include "PostProcess/SSAO.h"
#include "PostProcess/SSR.h"
#include "PostProcess/TAA.h"

#define IGNORE_SCRIPTS

namespace pe
{
	class Renderer
	{
	protected:
		Shadows shadows;
		Deferred deferred;
		SSAO ssao;
		SSR ssr;
		FXAA fxaa;
		TAA taa;
		Bloom bloom;
		DOF dof;
		MotionBlur motionBlur;
		SkyBox skyBoxDay;
		SkyBox skyBoxNight;
		GUI gui;
		Compute animationsCompute;
		Compute nodesCompute;
		
		std::vector<GPUTimer> metrics {};

#ifndef IGNORE_SCRIPTS
		std::vector<Script*> scripts{};
#endif
	public:
		Renderer();
		
		~Renderer();
		
		void Draw();
		
		void AddRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags);
		
		void LoadResources();
		
		void CreateUniforms();
		
		void ResizeViewport(uint32_t width, uint32_t height);
		
		void RecreatePipelines();
		
		uint32_t previousImageIndex = 0;
		std::map<std::string, Image> renderTargets {};
	
	private:		
		static void CheckQueue();
		
		void ComputeAnimations();
		
		void RecordDeferredCmds(const uint32_t& imageIndex);
		
		void RecordShadowsCmds(const uint32_t& imageIndex);
	};
}