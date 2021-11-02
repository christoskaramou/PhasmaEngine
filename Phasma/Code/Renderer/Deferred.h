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

#include "Pipeline.h"
#include "Shadows.h"
#include "Skybox/Skybox.h"
#include "RenderPass.h"

namespace pe
{
	class Descriptor;
	class FrameBuffer;
	class Image;

	class Deferred
	{
	public:
		Deferred();
		
		~Deferred();
		
		RenderPass renderPass, compositionRenderPass;
		std::vector<FrameBuffer*> framebuffers {}, compositionFramebuffers {};
		Descriptor* DSComposition;
		Pipeline pipeline;
		Pipeline pipelineComposition;
		Image* ibl_brdf_lut;
		
		struct UBO
		{
			vec4 screenSpace[8];
		} ubo;
		Buffer* uniform;
		
		void batchStart(CommandBuffer* cmd, uint32_t imageIndex);
		
		static void batchEnd();
		
		void createDeferredUniforms(std::map<std::string, Image*>& renderTargets);
		
		void updateDescriptorSets(std::map<std::string, Image*>& renderTargets);
		
		void update(mat4& invViewProj);
		
		void draw(CommandBuffer* cmd, uint32_t imageIndex, Shadows& shadows, SkyBox& skybox);
		
		void createRenderPasses(std::map<std::string, Image*>& renderTargets);
		
		void createFrameBuffers(std::map<std::string, Image*>& renderTargets);
		
		void createGBufferFrameBuffers(std::map<std::string, Image*>& renderTargets);
		
		void createCompositionFrameBuffers(std::map<std::string, Image*>& renderTargets);
		
		void createPipelines(std::map<std::string, Image*>& renderTargets);
		
		void createGBufferPipeline(std::map<std::string, Image*>& renderTargets);
		
		void createCompositionPipeline(std::map<std::string, Image*>& renderTargets);
		
		void destroy();
	};
}