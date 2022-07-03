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
#include "Renderer/Pipeline.h"
#include "Shader/Shader.h"
#include "Renderer/Descriptor.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RHI.h"

namespace pe
{
    PipelineColorBlendAttachmentState::PipelineColorBlendAttachmentState()
    {
        blendEnable = 0;
        srcColorBlendFactor = BlendFactor::Zero;
        dstColorBlendFactor = BlendFactor::Zero;
        colorBlendOp = BlendOp::Add;
        srcAlphaBlendFactor = BlendFactor::Zero;
        dstAlphaBlendFactor = BlendFactor::Zero;
        alphaBlendOp = BlendOp::Add;
        colorWriteMask = {};
    }

    PipelineCreateInfo::PipelineCreateInfo()
    {
        blendEnable = false;
        pVertShader = nullptr;
        pFragShader = nullptr;
        pCompShader = nullptr;
        vertexInputBindingDescriptions = {};
        vertexInputAttributeDescriptions = {};
        width = 0.f;
        height = 0.f;
        cullMode = CullMode::Back;
        colorBlendAttachments = {};
        dynamicStates = {};
        pushConstantStage = ShaderStage::VertexBit;
        pushConstantSize = 0;
        descriptorSetLayouts = {};
        renderPass = nullptr;
        dynamicColorTargets = 0;
        colorFormats = nullptr;
        depthFormat = nullptr;
        pipelineCache = {};
    }

    PipelineCreateInfo::~PipelineCreateInfo()
    {
    }

    Pipeline::Pipeline(const PipelineCreateInfo &info)
    {
        this->info = info;

        if (info.pCompShader)
        {
            VkShaderModuleCreateInfo csmci{};
            csmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            csmci.codeSize = info.pCompShader->BytesCount();
            csmci.pCode = info.pCompShader->GetSpriv();

            VkPushConstantRange pcr{};
            pcr.stageFlags = Translate<VkShaderStageFlags>(info.pushConstantStage);
            pcr.offset = 0;
            pcr.size = info.pushConstantSize;

            std::vector<VkDescriptorSetLayout> layouts(info.descriptorSetLayouts.size());
            for (uint32_t i = 0; i < info.descriptorSetLayouts.size(); i++)
                layouts[i] = info.descriptorSetLayouts[i]->Handle();
            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();
            plci.pushConstantRangeCount = info.pushConstantSize ? 1 : 0;
            plci.pPushConstantRanges = info.pushConstantSize ? &pcr : nullptr;

            VkShaderModule module;
            PE_CHECK(vkCreateShaderModule(RHII.GetDevice(), &csmci, nullptr, &module));

            VkComputePipelineCreateInfo compinfo{};
            compinfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            compinfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            compinfo.stage.module = module;
            compinfo.stage.pName = "main";
            compinfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

            VkPipelineLayout vklayout;
            PE_CHECK(vkCreatePipelineLayout(RHII.GetDevice(), &plci, nullptr, &vklayout));
            layout = vklayout;
            compinfo.layout = layout;

            VkPipeline vkPipeline;
            PE_CHECK(vkCreateComputePipelines(RHII.GetDevice(), nullptr, 1, &compinfo, nullptr, &vkPipeline));
            m_handle = vkPipeline;

            vkDestroyShaderModule(RHII.GetDevice(), module, nullptr);
        }
        else
        {
            VkGraphicsPipelineCreateInfo pipeinfo{};
            pipeinfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

            VkShaderModuleCreateInfo vsmci{};
            vsmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vsmci.codeSize = info.pVertShader->BytesCount();
            vsmci.pCode = info.pVertShader->GetSpriv();

            VkShaderModule vertModule;
            PE_CHECK(vkCreateShaderModule(RHII.GetDevice(), &vsmci, nullptr, &vertModule));

            VkPipelineShaderStageCreateInfo pssci1{};
            pssci1.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pssci1.stage = VK_SHADER_STAGE_VERTEX_BIT;
            pssci1.module = vertModule;
            pssci1.pName = "main";

            VkShaderModuleCreateInfo fsmci{};
            VkShaderModule fragModule;
            VkPipelineShaderStageCreateInfo pssci2{};
            if (info.pFragShader)
            {
                fsmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                fsmci.codeSize = info.pFragShader->BytesCount();
                fsmci.pCode = info.pFragShader->GetSpriv();
                PE_CHECK(vkCreateShaderModule(RHII.GetDevice(), &fsmci, nullptr, &fragModule));

                pssci2.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                pssci2.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                pssci2.module = fragModule;
                pssci2.pName = "main";
            }

            std::vector<VkPipelineShaderStageCreateInfo> stages{pssci1};
            if (info.pFragShader)
                stages.push_back(pssci2);

            pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
            pipeinfo.pStages = stages.data();

            // Vertex Input state
            std::vector<VkVertexInputBindingDescription> vibd(info.vertexInputBindingDescriptions.size());
            for (uint32_t i = 0; i < info.vertexInputBindingDescriptions.size(); i++)
            {
                vibd[i].binding = info.vertexInputBindingDescriptions[i].binding;
                vibd[i].stride = info.vertexInputBindingDescriptions[i].stride;
                vibd[i].inputRate = Translate<VkVertexInputRate>(info.vertexInputBindingDescriptions[i].inputRate);
            }

            std::vector<VkVertexInputAttributeDescription> vida(info.vertexInputAttributeDescriptions.size());
            for (uint32_t i = 0; i < info.vertexInputAttributeDescriptions.size(); i++)
            {
                vida[i].location = info.vertexInputAttributeDescriptions[i].location;
                vida[i].binding = info.vertexInputAttributeDescriptions[i].binding;
                vida[i].format = Translate<VkFormat>(info.vertexInputAttributeDescriptions[i].format);
                vida[i].offset = info.vertexInputAttributeDescriptions[i].offset;
            }

            VkPipelineVertexInputStateCreateInfo pvisci{};
            pvisci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibd.size());
            pvisci.pVertexBindingDescriptions = vibd.data();
            pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(vida.size());
            pvisci.pVertexAttributeDescriptions = vida.data();
            pipeinfo.pVertexInputState = &pvisci;

            // Input Assembly stage
            VkPipelineInputAssemblyStateCreateInfo piasci{};
            piasci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            piasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            piasci.primitiveRestartEnable = VK_FALSE;
            pipeinfo.pInputAssemblyState = &piasci;

            // Dynamic states
            std::vector<VkDynamicState> ds(info.dynamicStates.size());
            bool dynamicViewport = false;
            bool dynamicScissor = false;
            for (uint32_t i = 0; i < info.dynamicStates.size(); i++)
            {
                if (info.dynamicStates[i] == DynamicState::Viewport)
                    dynamicViewport = true;
                if (info.dynamicStates[i] == DynamicState::Scissor)
                    dynamicScissor = true;
                ds[i] = Translate<VkDynamicState>(info.dynamicStates[i]);
            }

            VkPipelineDynamicStateCreateInfo dsi{};
            dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dsi.dynamicStateCount = static_cast<uint32_t>(ds.size());
            dsi.pDynamicStates = ds.data();
            pipeinfo.pDynamicState = &dsi;

            // Viewports and Scissors
            VkPipelineViewportStateCreateInfo pvsci{};
            pvsci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

            VkViewport vp{};
            if (!dynamicViewport)
            {
                vp.x = 0.0f;
                vp.y = 0.0f;
                vp.width = info.width;
                vp.height = info.height;
                vp.minDepth = 0.0f;
                vp.maxDepth = 1.0f;
                pvsci.pViewports = &vp;
            }

            VkRect2D r2d{};
            if (!dynamicScissor)
            {
                r2d.extent = VkExtent2D{static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height)};
                pvsci.pScissors = &r2d;
            }

            pvsci.viewportCount = 1;
            pvsci.scissorCount = 1;
            pipeinfo.pViewportState = &pvsci;

            // Rasterization state
            VkPipelineRasterizationStateCreateInfo prsci{};
            prsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            prsci.depthClampEnable = VK_FALSE;
            prsci.rasterizerDiscardEnable = VK_FALSE;
            prsci.polygonMode = VK_POLYGON_MODE_FILL;
            prsci.cullMode = Translate<VkCullModeFlags>(info.cullMode);
            prsci.frontFace = VK_FRONT_FACE_CLOCKWISE;
            prsci.depthBiasEnable = VK_FALSE;
            prsci.depthBiasConstantFactor = 0.0f;
            prsci.depthBiasClamp = 0.0f;
            prsci.depthBiasSlopeFactor = 0.0f;
            prsci.lineWidth = 1.0f;
            pipeinfo.pRasterizationState = &prsci;

            // Multisample state
            VkPipelineMultisampleStateCreateInfo pmsci{};
            pmsci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            pmsci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pmsci.sampleShadingEnable = VK_FALSE;
            pmsci.minSampleShading = 1.0f;
            pmsci.pSampleMask = nullptr;
            pmsci.alphaToCoverageEnable = VK_FALSE;
            pmsci.alphaToOneEnable = VK_FALSE;
            pipeinfo.pMultisampleState = &pmsci;

            // Depth stencil state
            VkPipelineDepthStencilStateCreateInfo pdssci{};
            pdssci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            pdssci.depthTestEnable = VK_TRUE;
            pdssci.depthWriteEnable = VK_TRUE;
            pdssci.depthCompareOp = GlobalSettings::ReverseZ ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
            pdssci.depthBoundsTestEnable = VK_FALSE;
            pdssci.stencilTestEnable = VK_FALSE;
            pdssci.front.compareOp = VK_COMPARE_OP_ALWAYS;
            pdssci.back.compareOp = VK_COMPARE_OP_ALWAYS;
            pdssci.minDepthBounds = 0.0f;
            pdssci.maxDepthBounds = 0.0f;
            pipeinfo.pDepthStencilState = &pdssci;

            // Color Blending state
            std::vector<VkPipelineColorBlendAttachmentState> pcbast(info.colorBlendAttachments.size());
            for (uint32_t i = 0; i < info.colorBlendAttachments.size(); i++)
            {
                pcbast[i].blendEnable = info.colorBlendAttachments[i].blendEnable;
                pcbast[i].srcColorBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].srcColorBlendFactor);
                pcbast[i].dstColorBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].dstColorBlendFactor);
                pcbast[i].colorBlendOp = Translate<VkBlendOp>(info.colorBlendAttachments[i].colorBlendOp);
                pcbast[i].srcAlphaBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].srcAlphaBlendFactor);
                pcbast[i].dstAlphaBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].dstAlphaBlendFactor);
                pcbast[i].alphaBlendOp = Translate<VkBlendOp>(info.colorBlendAttachments[i].alphaBlendOp);
                pcbast[i].colorWriteMask = Translate<VkColorComponentFlags>(info.colorBlendAttachments[i].colorWriteMask);
            }

            VkPipelineColorBlendStateCreateInfo pcbsci{};
            pcbsci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            pcbsci.logicOpEnable = VK_FALSE;
            pcbsci.logicOp = VK_LOGIC_OP_AND;
            pcbsci.attachmentCount = static_cast<uint32_t>(pcbast.size());
            pcbsci.pAttachments = pcbast.data();
            pcbsci.blendConstants[0] = 0.0f;
            pcbsci.blendConstants[1] = 0.0f;
            pcbsci.blendConstants[2] = 0.0f;
            pcbsci.blendConstants[3] = 0.0f;
            pipeinfo.pColorBlendState = &pcbsci;

            // Push Constant Range
            VkPushConstantRange pcr{};
            pcr.size = info.pushConstantSize;
            pcr.offset = 0;
            pcr.stageFlags = Translate<VkShaderStageFlags>(info.pushConstantStage);

            // Pipeline Layout
            std::vector<VkDescriptorSetLayout> layouts{};
            if (info.descriptorSetLayouts.size() > 0)
            {
                for (uint32_t i = 0; i < info.descriptorSetLayouts.size(); i++)
                    layouts.push_back(info.descriptorSetLayouts[i]->Handle());
            }

            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();
            plci.pushConstantRangeCount = info.pushConstantSize ? 1 : 0;
            plci.pPushConstantRanges = info.pushConstantSize ? &pcr : nullptr;

            VkPipelineLayout pipelineLayout;
            PE_CHECK(vkCreatePipelineLayout(RHII.GetDevice(), &plci, nullptr, &pipelineLayout));
            layout = pipelineLayout;
            pipeinfo.layout = layout;

            // Dynamic Rendering
            VkPipelineRenderingCreateInfo prci{};
            prci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

            std::vector<VkFormat> vkFormats(info.dynamicColorTargets);
            if (info.dynamicColorTargets > 0 || info.depthFormat)
            {
                for (uint32_t i = 0; i < info.dynamicColorTargets; i++)
                    vkFormats[i] = Translate<VkFormat>(info.colorFormats[i]);

                prci.colorAttachmentCount = info.dynamicColorTargets;
                prci.pColorAttachmentFormats = vkFormats.data();

                if (info.depthFormat)
                {
                    prci.depthAttachmentFormat = Translate<VkFormat>(*info.depthFormat);
                    prci.stencilAttachmentFormat = Translate<VkFormat>(*info.depthFormat);
                }

                pipeinfo.pNext = &prci;
                pipeinfo.renderPass = nullptr;
            }
            else
            {
                // Render Pass
                pipeinfo.renderPass = info.renderPass->Handle();
            }

            // Subpass (Index of subpass this pipeline will be used in)
            pipeinfo.subpass = 0;

            // Base Pipeline Handle
            pipeinfo.basePipelineHandle = nullptr;

            // Base Pipeline Index
            pipeinfo.basePipelineIndex = -1;

            VkPipeline pipeline;
            PE_CHECK(vkCreateGraphicsPipelines(RHII.GetDevice(), nullptr, 1, &pipeinfo, nullptr, &pipeline));
            m_handle = pipeline;

            vkDestroyShaderModule(RHII.GetDevice(), vertModule, nullptr);
            if (info.pFragShader && fragModule)
                vkDestroyShaderModule(RHII.GetDevice(), fragModule, nullptr);

            Debug::SetObjectName(m_handle, ObjectType::Pipeline, info.name);
        }
    }

    Pipeline::~Pipeline()
    {
        if (layout)
        {
            vkDestroyPipelineLayout(RHII.GetDevice(), layout, nullptr);
            layout = {};
        }

        if (m_handle)
        {
            vkDestroyPipeline(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }
    }
}
#endif
