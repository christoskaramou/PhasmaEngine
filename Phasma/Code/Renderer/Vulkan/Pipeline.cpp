#if PE_VULKAN
#include "Renderer/Pipeline.h"
#include "Shader/Shader.h"
#include "Renderer/Descriptor.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RHI.h"

namespace pe
{
    PipelineColorBlendAttachmentState PipelineColorBlendAttachmentState::Default= {
        .srcColorBlendFactor = BlendFactor::SrcAlpha,
        .dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha,
        .colorBlendOp = BlendOp::Add,
        .srcAlphaBlendFactor = BlendFactor::One,
        .dstAlphaBlendFactor = BlendFactor::Zero,
        .alphaBlendOp = BlendOp::Add,
        .colorWriteMask = ColorComponent::RGBABit};

    PipelineColorBlendAttachmentState PipelineColorBlendAttachmentState::AdditiveColor = {
        .srcColorBlendFactor = BlendFactor::One,
        .dstColorBlendFactor = BlendFactor::One,
        .colorBlendOp = BlendOp::Add,
        .srcAlphaBlendFactor = BlendFactor::One,
        .dstAlphaBlendFactor = BlendFactor::One,
        .alphaBlendOp = BlendOp::Add,
        .colorWriteMask = ColorComponent::RGBABit};

    void PassInfo::ReflectDescriptors()
    {
        for (auto &descriptors : m_descriptors)
        {
            descriptors.clear();
            descriptors = Shader::ReflectPassDescriptors(*this);
        }
    }

    void PassInfo::UpdateHash()
    {
        m_hash = {};

        for (auto &descriptors : m_descriptors)
        {
            for (auto *descriptor : descriptors)
            {
                if (descriptor)
                {
                    m_hash.Combine(reinterpret_cast<intptr_t>(descriptor->GetLayout()));
                }
            }
        }

        if (pCompShader)
        {
            m_hash.Combine(pCompShader->GetCache().GetHash());
        }
        else
        {
            m_hash.Combine(static_cast<uint64_t>(topology));
            m_hash.Combine(static_cast<uint64_t>(polygonMode));
            m_hash.Combine(static_cast<uint64_t>(cullMode));
            m_hash.Combine(lineWidth);

            m_hash.Combine(blendEnable);

            for (auto &attachment : colorBlendAttachments)
            {
                m_hash.Combine(static_cast<uint64_t>(attachment.srcColorBlendFactor));
                m_hash.Combine(static_cast<uint64_t>(attachment.dstColorBlendFactor));
                m_hash.Combine(static_cast<uint64_t>(attachment.colorBlendOp));
                m_hash.Combine(static_cast<uint64_t>(attachment.srcAlphaBlendFactor));
                m_hash.Combine(static_cast<uint64_t>(attachment.dstAlphaBlendFactor));
                m_hash.Combine(static_cast<uint64_t>(attachment.alphaBlendOp));
                m_hash.Combine(attachment.colorWriteMask.Value());
            }

            for (auto &dynamic : dynamicStates)
            {
                m_hash.Combine(static_cast<uint64_t>(dynamic));
            }

            if (pVertShader)
            {
                m_hash.Combine(pVertShader->GetCache().GetHash());
            }

            if (pFragShader)
            {
                m_hash.Combine(pFragShader->GetCache().GetHash());
            }

            for (auto &colorFormat : colorFormats)
            {
                m_hash.Combine(static_cast<uint64_t>(colorFormat));
            }

            m_hash.Combine(static_cast<uint64_t>(depthFormat));
            m_hash.Combine(depthWriteEnable);
            m_hash.Combine(depthTestEnable);
            m_hash.Combine(static_cast<uint64_t>(depthCompareOp));

            m_hash.Combine(stencilTestEnable);
            m_hash.Combine(static_cast<uint64_t>(stencilFailOp));
            m_hash.Combine(static_cast<uint64_t>(stencilPassOp));
            m_hash.Combine(static_cast<uint64_t>(stencilDepthFailOp));
            m_hash.Combine(static_cast<uint64_t>(stencilCompareOp));
            m_hash.Combine(stencilCompareMask);
            m_hash.Combine(stencilWriteMask);
            m_hash.Combine(stencilReference);

            m_hash.Combine(reinterpret_cast<intptr_t>(pipelineCache.Get()));
        }
    }

    PassInfo::PassInfo()
        : pVertShader{},
          pFragShader{},
          pCompShader{},
          topology{PrimitiveTopology::TriangleList},
          polygonMode{PolygonMode::Fill},
          cullMode{CullMode::Back},
          lineWidth{1.0f},
          blendEnable{false},
          colorBlendAttachments{},
          dynamicStates{},
          colorFormats{},
          depthFormat{Format::Undefined},
          depthWriteEnable{true},
          depthTestEnable{true},
          depthCompareOp{Settings::Get<GlobalSettings>().reverse_depth ? CompareOp::GreaterOrEqual : CompareOp::LessOrEqual},
          pipelineCache{},
          stencilTestEnable{false},
          stencilFailOp{StencilOp::Keep},
          stencilPassOp{StencilOp::Replace},
          stencilDepthFailOp{StencilOp::Keep},
          stencilCompareOp{CompareOp::Always},
          stencilCompareMask{0x00u},
          stencilWriteMask{0x00u},
          stencilReference{0}
    {
    }

    PassInfo::~PassInfo()
    {
        Shader::Destroy(pCompShader);
        pCompShader = nullptr;

        Shader::Destroy(pVertShader);
        pVertShader = nullptr;

        Shader::Destroy(pFragShader);
        pFragShader = nullptr;
    }

    Pipeline::Pipeline(RenderPass *renderPass, PassInfo &info) : m_info(info)
    {
        if (info.pCompShader)
        {
            VkShaderModuleCreateInfo csmci{};
            csmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            csmci.codeSize = info.pCompShader->BytesCount();
            csmci.pCode = info.pCompShader->GetSpriv();

            VkPushConstantRange pcr{};
            const PushConstantDesc &pushConstantComp = info.pCompShader->GetPushConstantDesc();
            if (pushConstantComp.size > 0)
            {
                pcr.size = static_cast<uint32_t>(pushConstantComp.size);
                pcr.offset = 0;
                pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                PE_ERROR_IF(pcr.size > 128, "Compute push constant size is greater than 128 bytes");
                info.m_pushConstantStages.push_back(ShaderStage::ComputeBit);
                info.m_pushConstantOffsets.push_back(pcr.offset);
                info.m_pushConstantSizes.push_back(pcr.size);
            }

            std::vector<VkDescriptorSetLayout> layouts{};
            const auto &descriptors = info.GetDescriptors(0);
            for (uint32_t i = 0; i < descriptors.size(); i++)
                layouts.push_back(descriptors[i]->GetLayout()->ApiHandle());

            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();
            plci.pushConstantRangeCount = pcr.size ? 1 : 0;
            plci.pPushConstantRanges = pcr.size ? &pcr : nullptr;

            VkShaderModule module;
            PE_CHECK(vkCreateShaderModule(RHII.GetDevice(), &csmci, nullptr, &module));

            VkComputePipelineCreateInfo compinfo{};
            compinfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            compinfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            compinfo.stage.module = module;
            compinfo.stage.pName = info.pCompShader->GetEntryName().c_str();
            compinfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

            VkPipelineLayout vklayout;
            PE_CHECK(vkCreatePipelineLayout(RHII.GetDevice(), &plci, nullptr, &vklayout));
            m_layout = vklayout;
            compinfo.layout = m_layout;

            VkPipeline vkPipeline;
            PE_CHECK(vkCreateComputePipelines(RHII.GetDevice(), nullptr, 1, &compinfo, nullptr, &vkPipeline));
            m_apiHandle = vkPipeline;

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
            pssci1.pName = info.pVertShader->GetEntryName().c_str();

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
                pssci2.pName = info.pFragShader->GetEntryName().c_str();
            }

            std::vector<VkPipelineShaderStageCreateInfo> stages{pssci1};
            if (info.pFragShader)
                stages.push_back(pssci2);

            pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
            pipeinfo.pStages = stages.data();

            // Vertex Input state
            std::vector<VkVertexInputBindingDescription> vibds{};
            for (auto &vertexBinding : info.pVertShader->GetReflection().GetVertexBindings())
            {
                VkVertexInputBindingDescription vibd{};
                vibd.binding = vertexBinding.binding;
                vibd.stride = vertexBinding.stride;
                vibd.inputRate = Translate<VkVertexInputRate>(vertexBinding.inputRate);
                vibds.push_back(vibd);
            }
            std::vector<VkVertexInputAttributeDescription> vidas{};
            for (auto &vertexAttribute : info.pVertShader->GetReflection().GetVertexAttributes())
            {
                VkVertexInputAttributeDescription vida{};
                vida.location = vertexAttribute.location;
                vida.binding = vertexAttribute.binding;
                vida.format = Translate<VkFormat>(vertexAttribute.format);
                vida.offset = vertexAttribute.offset;
                vidas.push_back(vida);
            }
            VkPipelineVertexInputStateCreateInfo pvisci{};
            pvisci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibds.size());
            pvisci.pVertexBindingDescriptions = vibds.data();
            pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(vidas.size());
            pvisci.pVertexAttributeDescriptions = vidas.data();
            pipeinfo.pVertexInputState = &pvisci;

            // Input Assembly stage
            VkPipelineInputAssemblyStateCreateInfo piasci{};
            piasci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            piasci.topology = Translate<VkPrimitiveTopology>(info.topology);
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
            pvsci.viewportCount = 1;
            pvsci.scissorCount = 1;
            pipeinfo.pViewportState = &pvsci;

            // Rasterization state
            VkPipelineRasterizationStateCreateInfo prsci{};
            prsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            prsci.depthClampEnable = VK_FALSE;
            prsci.rasterizerDiscardEnable = VK_FALSE;
            prsci.polygonMode = Translate<VkPolygonMode>(info.polygonMode);
            prsci.cullMode = Translate<VkCullModeFlags>(info.cullMode);
            prsci.frontFace = VK_FRONT_FACE_CLOCKWISE;
            prsci.depthBiasEnable = VK_FALSE;
            prsci.depthBiasConstantFactor = 0.0f;
            prsci.depthBiasClamp = 0.0f;
            prsci.depthBiasSlopeFactor = 0.0f;
            prsci.lineWidth = max(info.lineWidth, 1.0f);
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
            pdssci.depthTestEnable = info.depthTestEnable;
            pdssci.depthWriteEnable = info.depthWriteEnable;
            pdssci.depthCompareOp = Translate<VkCompareOp>(info.depthCompareOp);
            pdssci.stencilTestEnable = info.stencilTestEnable;
            pdssci.front.failOp = Translate<VkStencilOp>(info.stencilFailOp);
            pdssci.front.passOp = Translate<VkStencilOp>(info.stencilPassOp);
            pdssci.front.depthFailOp = Translate<VkStencilOp>(info.stencilDepthFailOp);
            pdssci.front.compareOp = Translate<VkCompareOp>(info.stencilCompareOp);
            pdssci.front.compareMask = info.stencilCompareMask;
            pdssci.front.writeMask = info.stencilWriteMask;
            pdssci.front.reference = info.stencilReference;
            pdssci.back = pdssci.front;
            pdssci.depthBoundsTestEnable = VK_FALSE;
            pdssci.minDepthBounds = 0.0f;
            pdssci.maxDepthBounds = 0.0f;
            pipeinfo.pDepthStencilState = &pdssci;

            // Color Blending state
            std::vector<VkPipelineColorBlendAttachmentState> pcbast(info.colorBlendAttachments.size());
            for (uint32_t i = 0; i < info.colorBlendAttachments.size(); i++)
            {
                pcbast[i] = {};
                if (info.blendEnable)
                {
                    pcbast[i].blendEnable = VK_TRUE;
                    pcbast[i].srcColorBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].srcColorBlendFactor);
                    pcbast[i].dstColorBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].dstColorBlendFactor);
                    pcbast[i].colorBlendOp = Translate<VkBlendOp>(info.colorBlendAttachments[i].colorBlendOp);
                    pcbast[i].srcAlphaBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].srcAlphaBlendFactor);
                    pcbast[i].dstAlphaBlendFactor = Translate<VkBlendFactor>(info.colorBlendAttachments[i].dstAlphaBlendFactor);
                    pcbast[i].alphaBlendOp = Translate<VkBlendOp>(info.colorBlendAttachments[i].alphaBlendOp);
                    pcbast[i].colorWriteMask = Translate<VkColorComponentFlags>(info.colorBlendAttachments[i].colorWriteMask);
                }
                else
                {
                    pcbast[i].blendEnable = VK_FALSE;
                    pcbast[i].colorWriteMask = Translate<VkColorComponentFlags>(info.colorBlendAttachments[i].colorWriteMask);
                }
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
            std::vector<VkPushConstantRange> pcrs;
            pcrs.reserve(2);
            const PushConstantDesc &pushConstantVert = info.pVertShader ? info.pVertShader->GetPushConstantDesc() : PushConstantDesc{};
            const PushConstantDesc &pushConstantFrag = info.pFragShader ? info.pFragShader->GetPushConstantDesc() : PushConstantDesc{};
            if (pushConstantVert.size > 0 && pushConstantVert == pushConstantFrag)
            {
                VkPushConstantRange vertPcr{};
                vertPcr.size = static_cast<uint32_t>(pushConstantVert.size);
                vertPcr.offset = 0;
                vertPcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                pcrs.push_back(vertPcr);
                PE_ERROR_IF(vertPcr.size > 128, "Push constant size is greater than 128 bytes");

                info.m_pushConstantStages.push_back(ShaderStage::VertexBit | ShaderStage::FragmentBit);
                info.m_pushConstantOffsets.push_back(vertPcr.offset);
                info.m_pushConstantSizes.push_back(vertPcr.size);
            }
            else
            {
                VkPushConstantRange vertPcr{};
                VkPushConstantRange fragPcr{};
                if (pushConstantVert.size > 0)
                {
                    vertPcr.size = static_cast<uint32_t>(pushConstantVert.size);
                    vertPcr.offset = 0;
                    vertPcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                    pcrs.push_back(vertPcr);
                    PE_ERROR_IF(vertPcr.size > 128, "Vertex push constant size is greater than 128 bytes");

                    info.m_pushConstantStages.push_back(ShaderStage::VertexBit);
                    info.m_pushConstantOffsets.push_back(vertPcr.offset);
                    info.m_pushConstantSizes.push_back(vertPcr.size);
                }
                if (pushConstantFrag.size > 0)
                {
                    fragPcr.size = static_cast<uint32_t>(pushConstantFrag.size);
                    fragPcr.offset = vertPcr.size;
                    fragPcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                    pcrs.push_back(fragPcr);
                    PE_ERROR_IF(fragPcr.offset + fragPcr.size > 128, "Fragment push constant size is greater than 128 bytes");

                    info.m_pushConstantStages.push_back(ShaderStage::FragmentBit);
                    info.m_pushConstantOffsets.push_back(fragPcr.offset);
                    info.m_pushConstantSizes.push_back(fragPcr.size);
                }
            }

            // Pipeline Layout
            std::vector<VkDescriptorSetLayout> layouts{};
            const auto &descriptors = info.GetDescriptors(0);
            for (uint32_t i = 0; i < descriptors.size(); i++)
                layouts.push_back(descriptors[i]->GetLayout()->ApiHandle());

            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();
            plci.pushConstantRangeCount = static_cast<uint32_t>(pcrs.size());
            plci.pPushConstantRanges = pcrs.data();

            VkPipelineLayout pipelineLayout;
            PE_CHECK(vkCreatePipelineLayout(RHII.GetDevice(), &plci, nullptr, &pipelineLayout));
            m_layout = pipelineLayout;
            pipeinfo.layout = m_layout;

            // Render Pass
            uint32_t colorAttachmentCount = static_cast<uint32_t>(info.colorBlendAttachments.size());
            std::vector<VkFormat> vkFormats(colorAttachmentCount);
            VkPipelineRenderingCreateInfo prci{};
            if (Settings::Get<GlobalSettings>().dynamic_rendering)
            {
                prci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

                for (uint32_t i = 0; i < colorAttachmentCount; i++)
                {
                    vkFormats[i] = Translate<VkFormat>(info.colorFormats[i]);
                }

                prci.colorAttachmentCount = colorAttachmentCount;
                prci.pColorAttachmentFormats = vkFormats.data();

                if (HasDepth(info.depthFormat))
                {
                    VkFormat depthFormat = Translate<VkFormat>(info.depthFormat);
                    prci.depthAttachmentFormat = depthFormat;
                    if (HasStencil(info.depthFormat))
                        prci.stencilAttachmentFormat = depthFormat;
                }

                pipeinfo.pNext = &prci;
                pipeinfo.renderPass = nullptr;
            }
            else
            {
                PE_ERROR_IF(!renderPass, "RenderPass is null");
                pipeinfo.renderPass = renderPass->ApiHandle();
            }

            // Subpass (Index of subpass this pipeline will be used in)
            pipeinfo.subpass = 0;

            // Base Pipeline ApiHandle
            pipeinfo.basePipelineHandle = nullptr;

            // Base Pipeline Index
            pipeinfo.basePipelineIndex = -1;

            VkPipeline pipeline;
            PE_CHECK(vkCreateGraphicsPipelines(RHII.GetDevice(), nullptr, 1, &pipeinfo, nullptr, &pipeline));
            m_apiHandle = pipeline;

            vkDestroyShaderModule(RHII.GetDevice(), vertModule, nullptr);
            if (info.pFragShader && fragModule)
                vkDestroyShaderModule(RHII.GetDevice(), fragModule, nullptr);

            Debug::SetObjectName(m_apiHandle, info.name);
        }
    }

    Pipeline::~Pipeline()
    {
        if (m_layout)
        {
            vkDestroyPipelineLayout(RHII.GetDevice(), m_layout, nullptr);
            m_layout = {};
        }

        if (m_apiHandle)
        {
            vkDestroyPipeline(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }
}
#endif
