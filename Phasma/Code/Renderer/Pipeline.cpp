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
#include "Pipeline.h"
#include "Shader/Shader.h"
#include "Descriptor.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Core/Settings.h"

namespace pe
{
	PipelineCreateInfo::PipelineCreateInfo()
	{
		blendEnable = false;
		pVertShader = nullptr;
		pFragShader = nullptr;
		pCompShader = nullptr;
		vertexInputBindingDescriptions = make_sptr(std::vector<vk::VertexInputBindingDescription>());
		vertexInputAttributeDescriptions = make_sptr(std::vector<vk::VertexInputAttributeDescription>());
		width = 0.f;
		height = 0.f;
		pushConstantStage = PushConstantStage::Vertex;
		pushConstantSize = 0;
		colorBlendAttachments = make_sptr(std::vector<vk::PipelineColorBlendAttachmentState>());
		dynamicStates = make_sptr(std::vector<vk::DynamicState>());
		descriptorSetLayouts = make_sptr(std::vector<vk::DescriptorSetLayout>());
		pipelineCache = make_sptr(vk::PipelineCache());
	}
	
	PipelineCreateInfo::~PipelineCreateInfo()
	{
	}
	
	Pipeline::Pipeline()
	{
		handle = make_sptr(vk::Pipeline());
		layout = make_sptr(vk::PipelineLayout());
	}
	
	Pipeline::~Pipeline()
	{
	}
	
	void Pipeline::createGraphicsPipeline()
	{
		vk::GraphicsPipelineCreateInfo pipeinfo;
		
		vk::ShaderModuleCreateInfo vsmci;
		vsmci.codeSize = info.pVertShader->BytesCount();
		vsmci.pCode = info.pVertShader->GetSpriv();
		vk::UniqueShaderModule vertModule = VULKAN.device->createShaderModuleUnique(vsmci);
		
		vk::PipelineShaderStageCreateInfo pssci1;
		pssci1.stage = vk::ShaderStageFlagBits::eVertex;
		pssci1.module = vertModule.get();
		pssci1.pName = "main";
		
		vk::ShaderModuleCreateInfo fsmci;
		vk::UniqueShaderModule fragModule;
		vk::PipelineShaderStageCreateInfo pssci2;
		if (info.pFragShader)
		{
			fsmci.codeSize = info.pFragShader->BytesCount();
			fsmci.pCode = info.pFragShader->GetSpriv();
			fragModule = VULKAN.device->createShaderModuleUnique(fsmci);
			
			pssci2.stage = vk::ShaderStageFlagBits::eFragment;
			pssci2.module = fragModule.get();
			pssci2.pName = "main";
		}
		
		std::vector<vk::PipelineShaderStageCreateInfo> stages {pssci1};
		if (info.pFragShader)
			stages.push_back(pssci2);
		
		pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
		pipeinfo.pStages = stages.data();
		
		// Vertex Input state
		vk::PipelineVertexInputStateCreateInfo pvisci;
		pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(info.vertexInputBindingDescriptions->size());
		pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(info.vertexInputAttributeDescriptions->size());
		pvisci.pVertexBindingDescriptions = info.vertexInputBindingDescriptions->data();
		pvisci.pVertexAttributeDescriptions = info.vertexInputAttributeDescriptions->data();
		pipeinfo.pVertexInputState = &pvisci;
		
		// Input Assembly stage
		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = vk::PrimitiveTopology::eTriangleList;
		piasci.primitiveRestartEnable = VK_FALSE;
		pipeinfo.pInputAssemblyState = &piasci;
		
		// Viewports and Scissors
		vk::Viewport vp;
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = info.width;
		vp.height = info.height;
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;
		
		vk::Rect2D r2d;
		r2d.extent = vk::Extent2D {static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height)};
		
		vk::PipelineViewportStateCreateInfo pvsci;
		pvsci.viewportCount = 1;
		pvsci.pViewports = &vp;
		pvsci.scissorCount = 1;
		pvsci.pScissors = &r2d;
		pipeinfo.pViewportState = &pvsci;
		
		// Rasterization state
		vk::PipelineRasterizationStateCreateInfo prsci;
		prsci.depthClampEnable = VK_FALSE;
		prsci.rasterizerDiscardEnable = VK_FALSE;
		prsci.polygonMode = vk::PolygonMode::eFill;
		prsci.cullMode = static_cast<vk::CullModeFlagBits>(info.cullMode);
		prsci.frontFace = vk::FrontFace::eClockwise;
		prsci.depthBiasEnable = VK_FALSE;
		prsci.depthBiasConstantFactor = 0.0f;
		prsci.depthBiasClamp = 0.0f;
		prsci.depthBiasSlopeFactor = 0.0f;
		prsci.lineWidth = 1.0f;
		pipeinfo.pRasterizationState = &prsci;
		
		// Multisample state
		vk::PipelineMultisampleStateCreateInfo pmsci;
		pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
		pmsci.sampleShadingEnable = VK_FALSE;
		pmsci.minSampleShading = 1.0f;
		pmsci.pSampleMask = nullptr;
		pmsci.alphaToCoverageEnable = VK_FALSE;
		pmsci.alphaToOneEnable = VK_FALSE;
		pipeinfo.pMultisampleState = &pmsci;
		
		// Depth stencil state
		vk::PipelineDepthStencilStateCreateInfo pdssci;
		pdssci.depthTestEnable = VK_TRUE;
		pdssci.depthWriteEnable = VK_TRUE;
		pdssci.depthCompareOp = GlobalSettings::ReverseZ ? vk::CompareOp::eGreater : vk::CompareOp::eLessOrEqual;
		pdssci.depthBoundsTestEnable = VK_FALSE;
		pdssci.stencilTestEnable = VK_FALSE;
		pdssci.front.compareOp = vk::CompareOp::eAlways;
		pdssci.back.compareOp = vk::CompareOp::eAlways;
		pdssci.minDepthBounds = 0.0f;
		pdssci.maxDepthBounds = 0.0f;
		pipeinfo.pDepthStencilState = &pdssci;

		// Color Blending state
		vk::PipelineColorBlendStateCreateInfo pcbsci;
		pcbsci.logicOpEnable = VK_FALSE;
		pcbsci.logicOp = vk::LogicOp::eAnd;
		pcbsci.attachmentCount = static_cast<uint32_t>(info.colorBlendAttachments->size());
		pcbsci.pAttachments = info.colorBlendAttachments->data();
		float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
		pipeinfo.pColorBlendState = &pcbsci;
		
		// Dynamic state
		vk::PipelineDynamicStateCreateInfo dsi;
		dsi.dynamicStateCount = static_cast<uint32_t>(info.dynamicStates->size());
		dsi.pDynamicStates = info.dynamicStates->data();
		pipeinfo.pDynamicState = &dsi;
		
		// Push Constant Range
		vk::PushConstantRange pcr;
		pcr.stageFlags = static_cast<vk::ShaderStageFlagBits>(info.pushConstantStage);
		pcr.size = info.pushConstantSize;
		
		// Pipeline Layout
		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(info.descriptorSetLayouts->size());
		plci.pSetLayouts = info.descriptorSetLayouts->data();
		plci.pushConstantRangeCount = info.pushConstantSize ? 1 : 0;
		plci.pPushConstantRanges = info.pushConstantSize ? &pcr : nullptr;
		layout = make_sptr(VULKAN.device->createPipelineLayout(plci));
		pipeinfo.layout = *layout;
		
		// Render Pass
		pipeinfo.renderPass = info.renderPass.handle;
		
		// Subpass (Index of subpass this pipeline will be used in)
		pipeinfo.subpass = 0;
		
		// Base Pipeline Handle
		pipeinfo.basePipelineHandle = nullptr;
		
		// Base Pipeline Index
		pipeinfo.basePipelineIndex = -1;
		
		handle = make_sptr(VULKAN.device->createGraphicsPipeline(nullptr, pipeinfo).value);

		//vk::PipelineCacheCreateInfo pcci;
		//VULKAN.device->createPipelineCache()
	}
	
	void Pipeline::createComputePipeline()
	{
		vk::ComputePipelineCreateInfo compinfo;
		
		vk::ShaderModuleCreateInfo csmci;
		csmci.codeSize = info.pCompShader->BytesCount();
		csmci.pCode = info.pCompShader->GetSpriv();
		
		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = static_cast<uint32_t>(info.descriptorSetLayouts->size());
		plci.pSetLayouts = info.descriptorSetLayouts->data();
		
		vk::UniqueShaderModule module = VULKAN.device->createShaderModuleUnique(csmci);
		
		compinfo.stage.module = module.get();
		compinfo.stage.pName = "main";
		compinfo.stage.stage = vk::ShaderStageFlagBits::eCompute;
		layout = make_sptr(VULKAN.device->createPipelineLayout(plci));
		compinfo.layout = *layout;
		
		handle = make_sptr(VULKAN.device->createComputePipeline(nullptr, compinfo).value);
	}
	
	void Pipeline::destroy()
	{
		if (*layout)
		{
			VULKAN.device->destroyPipelineLayout(*layout);
			*layout = nullptr;
		}
		
		if (*handle)
		{
			VULKAN.device->destroyPipeline(*handle);
			*handle = nullptr;
		}
	}

	DescriptorSetLayoutHandle CreateDescriptorSetLayout(const std::vector<DescriptorBinding>& descriptionBindings)
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
		for (const auto& layoutBinding : descriptionBindings)
		{
			setLayoutBindings.push_back(
				VkDescriptorSetLayoutBinding
				{
					layoutBinding.binding,
					(VkDescriptorType)layoutBinding.descriptorType,
					1,
					(VkShaderStageFlags)layoutBinding.stageFlags,
					nullptr
				}
			);
		}

		VkDescriptorSetLayoutCreateInfo descriptorLayout{};
		descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();

		VkDescriptorSetLayout layout;
		vkCreateDescriptorSetLayout(*VULKAN.device, &descriptorLayout, nullptr, &layout);

		return layout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutComposition()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(2, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(3, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(4, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(5, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(6, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(7, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(8, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(9, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "Composition");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutBrightFilter()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "BrightFilter");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutGaussianBlurH()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "GaussianBlurH");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutGaussianBlurV()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "GaussianBlurV");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutCombine()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "Combine");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutDOF()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "DOF");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutFXAA()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "FXAA");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutMotionBlur()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(2, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(3, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "MotionBlur");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutSSAO()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(2, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(3, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(4, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "SSAO");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutSSAOBlur()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "SSAOBlur");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutSSR()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(2, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(3, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(4, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "SSR");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutTAA()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(2, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(3, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(4, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "TAA");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutTAASharpen()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "TAASharpen");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutShadows()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					//DescriptorBinding(0, (uint32_t)vk::DescriptorType::eUniformBuffer, (uint32_t)vk::ShaderStageFlagBits::eVertex)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "Shadows");
		}
		
		return DSLayout;
	}

	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutShadowsDeferred()
	{
		static DescriptorSetLayoutHandle DSLayout = {};

		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(2, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(3, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(4, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "ShadowCascades");
		}

		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutMesh()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eUniformBuffer, (uint32_t)vk::ShaderStageFlagBits::eVertex)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "Mesh");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutPrimitive()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(2, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(3, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(4, (uint32_t)vk::DescriptorType::eCombinedImageSampler,	(uint32_t)vk::ShaderStageFlagBits::eFragment),
					DescriptorBinding(5, (uint32_t)vk::DescriptorType::eUniformBuffer,			(uint32_t)vk::ShaderStageFlagBits::eVertex)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "Primitive");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutModel()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eUniformBuffer, (uint32_t)vk::ShaderStageFlagBits::eVertex)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "Model");
		}
		
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutSkybox()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eCombinedImageSampler, (uint32_t)vk::ShaderStageFlagBits::eFragment)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "SkyBox");
		}
		return DSLayout;
	}
	
	DescriptorSetLayoutHandle& Pipeline::getDescriptorSetLayoutCompute()
	{
		static DescriptorSetLayoutHandle DSLayout = {};
		
		if (!VkDescriptorSetLayout(DSLayout))
		{
			DSLayout = CreateDescriptorSetLayout(
				{
					DescriptorBinding(0, (uint32_t)vk::DescriptorType::eStorageBuffer, (uint32_t)vk::ShaderStageFlagBits::eCompute),
					DescriptorBinding(1, (uint32_t)vk::DescriptorType::eStorageBuffer, (uint32_t)vk::ShaderStageFlagBits::eCompute)
				});
			VULKAN.SetDebugObjectName(vk::DescriptorSetLayout(DSLayout), "Compute");
		}
		
		return DSLayout;
	}
}