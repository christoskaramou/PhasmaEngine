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

#define IGNORE_SCRIPTS

namespace pe
{
	class CommandBuffer;
	class Image;

	class Viewport
	{
	public:
		float x;
		float y;
		float width;
		float height;
		float minDepth;
		float maxDepth;
	};

	class RenderArea
	{
	public:
		Viewport viewport;
		Rect2D scissor;

		void Update(float x, float y, float w, float h, float minDepth = 0.f, float maxDepth = 1.f);
	};

	class Renderer
	{
	protected:
		RenderArea renderArea;
		Shadows shadows;
		Deferred deferred;
		SkyBox skyBoxDay;
		SkyBox skyBoxNight;
		GUI gui;
		Compute animationsCompute;
		Compute nodesCompute;

#ifndef IGNORE_SCRIPTS
		std::vector<Script*> scripts{};
#endif
	public:
		Renderer();
		
		~Renderer();

		RenderArea& GetRenderArea() { return renderArea; }
		
		void AddRenderTarget(const std::string& name, Format format, ImageUsageFlags additionalFlags = 0);
		
		void LoadResources();
		
		void CreateUniforms();
		
		void ResizeViewport(uint32_t width, uint32_t height);

		void BlitToViewport(CommandBuffer* cmd, Image* renderedImage, uint32_t imageIndex);
		
		void RecreatePipelines();

		std::map<std::string, Image*>& GetRenderTargets() { return renderTargets; }

		std::map<std::string, Image*> renderTargets {};
	
	protected:		
		static void CheckQueue();
		
		void ComputeAnimations();
		
		void RecordDeferredCmds(uint32_t imageIndex);
		
		void RecordShadowsCmds(uint32_t imageIndex);
	};
}