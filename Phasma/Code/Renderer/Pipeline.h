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

#include "Core/Base.h"
#include "Shader/Shader.h"
#include "Renderer/RenderPass.h"

namespace pe
{
	enum class CullMode
	{
		None = 0,
		Front = 1,
		Back = 2
	};
	
	enum class PushConstantStage
	{
		Vertex = 1,
		Fragment = 16,
		Compute = 32
	};
	
	class PipelineCreateInfo
	{
	public:
		PipelineCreateInfo();
		
		~PipelineCreateInfo();
		
		bool blendEnable;
		Shader* pVertShader;
		Shader* pFragShader;
		Shader* pCompShader;
		SPtr<std::vector<vk::VertexInputBindingDescription>> vertexInputBindingDescriptions;
		SPtr<std::vector<vk::VertexInputAttributeDescription>> vertexInputAttributeDescriptions;
		float width;
		float height;
		CullMode cullMode;
		SPtr<std::vector<vk::PipelineColorBlendAttachmentState>> colorBlendAttachments;
		SPtr<std::vector<vk::DynamicState>> dynamicStates;
		PushConstantStage pushConstantStage;
		uint32_t pushConstantSize;
		SPtr<std::vector<vk::DescriptorSetLayout>> descriptorSetLayouts;
		RenderPass renderPass;
		SPtr<vk::PipelineCache> pipelineCache;
	};
	
	class Pipeline
	{
	public:
		Pipeline();
		
		~Pipeline();
		
		PipelineCreateInfo info;
		SPtr<vk::Pipeline> handle;
		SPtr<vk::PipelineLayout> layout;
		
		void createGraphicsPipeline();
		
		void createComputePipeline();
		
		void destroy();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutComposition();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutBrightFilter();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutGaussianBlurH();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutGaussianBlurV();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutCombine();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutDOF();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutFXAA();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutMotionBlur();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutSSAO();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutSSAOBlur();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutSSR();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutTAA();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutTAASharpen();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutShadows();

		static DescriptorSetLayoutHandle& getDescriptorSetLayoutShadowsDeferred();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutMesh();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutPrimitive();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutModel();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutSkybox();
		
		static DescriptorSetLayoutHandle& getDescriptorSetLayoutCompute();
	};
}