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
        blendEnable = {};
        srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendOp = VK_BLEND_OP_ADD;
        srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        alphaBlendOp = VK_BLEND_OP_ADD;
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
        pushConstantStage = PushConstantStage::Vertex;
        pushConstantSize = 0;
        descriptorSetLayouts = {};
        renderPass = nullptr;
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

            std::vector<VkDescriptorSetLayout> layouts(info.descriptorSetLayouts.size());
            for (uint32_t i = 0; i < info.descriptorSetLayouts.size(); i++)
                layouts[i] = info.descriptorSetLayouts[i]->Handle();
            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();

            VkShaderModule module;
            vkCreateShaderModule(RHII.device, &csmci, nullptr, &module);

            VkComputePipelineCreateInfo compinfo{};
            compinfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            compinfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            compinfo.stage.module = module;
            compinfo.stage.pName = "main";
            compinfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

            VkPipelineLayout vklayout;
            vkCreatePipelineLayout(RHII.device, &plci, nullptr, &vklayout);
            layout = vklayout;
            compinfo.layout = layout;

            VkPipeline vkPipeline;
            vkCreateComputePipelines(RHII.device, nullptr, 1, &compinfo, nullptr, &vkPipeline);
            m_handle = vkPipeline;

            vkDestroyShaderModule(RHII.device, module, nullptr);
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
            vkCreateShaderModule(RHII.device, &vsmci, nullptr, &vertModule);

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
                vkCreateShaderModule(RHII.device, &fsmci, nullptr, &fragModule);

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
            VkPipelineVertexInputStateCreateInfo pvisci{};
            pvisci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(info.vertexInputBindingDescriptions.size());
            pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(info.vertexInputAttributeDescriptions.size());
            pvisci.pVertexBindingDescriptions = (VkVertexInputBindingDescription *)info.vertexInputBindingDescriptions.data();
            pvisci.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription *)info.vertexInputAttributeDescriptions.data();
            pipeinfo.pVertexInputState = &pvisci;

            // Input Assembly stage
            VkPipelineInputAssemblyStateCreateInfo piasci{};
            piasci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            piasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            piasci.primitiveRestartEnable = VK_FALSE;
            pipeinfo.pInputAssemblyState = &piasci;

            // Viewports and Scissors
            VkViewport vp{};
            vp.x = 0.0f;
            vp.y = 0.0f;
            vp.width = info.width;
            vp.height = info.height;
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;

            VkRect2D r2d{};
            r2d.extent = VkExtent2D{static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height)};

            VkPipelineViewportStateCreateInfo pvsci{};
            pvsci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            pvsci.viewportCount = 1;
            pvsci.pViewports = &vp;
            pvsci.scissorCount = 1;
            pvsci.pScissors = &r2d;
            pipeinfo.pViewportState = &pvsci;

            // Rasterization state
            VkPipelineRasterizationStateCreateInfo prsci{};
            prsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            prsci.depthClampEnable = VK_FALSE;
            prsci.rasterizerDiscardEnable = VK_FALSE;
            prsci.polygonMode = VK_POLYGON_MODE_FILL;
            prsci.cullMode = static_cast<VkCullModeFlagBits>(info.cullMode);
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
            pdssci.depthCompareOp = GlobalSettings::ReverseZ ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_LESS_OR_EQUAL;
            pdssci.depthBoundsTestEnable = VK_FALSE;
            pdssci.stencilTestEnable = VK_FALSE;
            pdssci.front.compareOp = VK_COMPARE_OP_ALWAYS;
            pdssci.back.compareOp = VK_COMPARE_OP_ALWAYS;
            pdssci.minDepthBounds = 0.0f;
            pdssci.maxDepthBounds = 0.0f;
            pipeinfo.pDepthStencilState = &pdssci;

            // Color Blending state
            VkPipelineColorBlendStateCreateInfo pcbsci{};
            pcbsci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            pcbsci.logicOpEnable = VK_FALSE;
            pcbsci.logicOp = VK_LOGIC_OP_AND;
            pcbsci.attachmentCount = static_cast<uint32_t>(info.colorBlendAttachments.size());
            pcbsci.pAttachments = (VkPipelineColorBlendAttachmentState *)info.colorBlendAttachments.data();
            float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
            pipeinfo.pColorBlendState = &pcbsci;

            // Dynamic state
            VkPipelineDynamicStateCreateInfo dsi{};
            dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dsi.dynamicStateCount = static_cast<uint32_t>(info.dynamicStates.size());
            dsi.pDynamicStates = (VkDynamicState *)info.dynamicStates.data();
            pipeinfo.pDynamicState = &dsi;

            // Push Constant Range
            VkPushConstantRange pcr{};
            pcr.stageFlags = static_cast<VkShaderStageFlags>(info.pushConstantStage);
            pcr.offset = 0;
            pcr.size = info.pushConstantSize;

            // Pipeline Layout
            std::vector<VkDescriptorSetLayout> layouts(info.descriptorSetLayouts.size());
            for (uint32_t i = 0; i < info.descriptorSetLayouts.size(); i++)
                layouts[i] = info.descriptorSetLayouts[i]->Handle();
            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();
            plci.pushConstantRangeCount = info.pushConstantSize ? 1 : 0;
            plci.pPushConstantRanges = info.pushConstantSize ? &pcr : nullptr;

            VkPipelineLayout pipelineLayout;
            vkCreatePipelineLayout(RHII.device, &plci, nullptr, &pipelineLayout);
            layout = pipelineLayout;
            pipeinfo.layout = layout;

            // Render Pass
            pipeinfo.renderPass = info.renderPass->Handle();

            // Subpass (Index of subpass this pipeline will be used in)
            pipeinfo.subpass = 0;

            // Base Pipeline Handle
            pipeinfo.basePipelineHandle = nullptr;

            // Base Pipeline Index
            pipeinfo.basePipelineIndex = -1;

            VkPipeline pipeline;
            vkCreateGraphicsPipelines(RHII.device, nullptr, 1, &pipeinfo, nullptr, &pipeline);
            m_handle = pipeline;

            vkDestroyShaderModule(RHII.device, vertModule, nullptr);
            if (info.pFragShader && fragModule)
                vkDestroyShaderModule(RHII.device, fragModule, nullptr);
        }
    }

    Pipeline::~Pipeline()
    {
        if (layout)
        {
            vkDestroyPipelineLayout(RHII.device, layout, nullptr);
            layout = {};
        }

        if (m_handle)
        {
            vkDestroyPipeline(RHII.device, m_handle, nullptr);
            m_handle = {};
        }
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutComposition()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutBrightFilter()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutGaussianBlurH()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutGaussianBlurV()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutCombine()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutDOF()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutFXAA()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutMotionBlur()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutSSAO()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutSSAOBlur()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutSSR()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutTAA()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutTAASharpen()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutShadows()
    {
        static std::vector<DescriptorBinding> bindings{
            // DescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        };
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutShadowsDeferred()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutMesh()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutPrimitive()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            DescriptorBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutModel()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutSkybox()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }

    DescriptorLayout *Pipeline::getDescriptorSetLayoutCompute()
    {
        static std::vector<DescriptorBinding> bindings{
            DescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
            DescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)};
        static DescriptorLayout *DSLayout = DescriptorLayout::Create(bindings);

        return DSLayout;
    }
}
#endif
