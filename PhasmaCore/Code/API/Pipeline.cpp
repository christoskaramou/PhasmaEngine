#include "API/Pipeline.h"
#include "API/Shader.h"
#include "API/Descriptor.h"
#include "API/RenderPass.h"
#include "API/RHI.h"

namespace pe
{
    auto PipelineColorBlendAttachmentState::Default = vk::PipelineColorBlendAttachmentState(
        /*.blendEnable            =*/ VK_TRUE,
        /*.srcColorBlendFactor    =*/ vk::BlendFactor::eSrcAlpha,
        /*.dstColorBlendFactor    =*/ vk::BlendFactor::eOneMinusSrcAlpha,
        /*.colorBlendOp           =*/ vk::BlendOp::eAdd,
        /*.srcAlphaBlendFactor    =*/ vk::BlendFactor::eOne,
        /*.dstAlphaBlendFactor    =*/ vk::BlendFactor::eZero,
        /*.alphaBlendOp           =*/ vk::BlendOp::eAdd,
        /*.colorWriteMask         =*/ vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto PipelineColorBlendAttachmentState::AdditiveColor = vk::PipelineColorBlendAttachmentState(
        /*.blendEnable            =*/ VK_TRUE,
        /*.srcColorBlendFactor    =*/ vk::BlendFactor::eOne,
        /*.dstColorBlendFactor    =*/ vk::BlendFactor::eOne,
        /*.colorBlendOp           =*/ vk::BlendOp::eAdd,
        /*.srcAlphaBlendFactor    =*/ vk::BlendFactor::eOne,
        /*.dstAlphaBlendFactor    =*/ vk::BlendFactor::eOne,
        /*.alphaBlendOp           =*/ vk::BlendOp::eAdd,
        /*.colorWriteMask         =*/ vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

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
            m_hash.Combine(static_cast<uint32_t>(cullMode));
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
                m_hash.Combine(static_cast<uint32_t>(attachment.colorWriteMask));
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

            m_hash.Combine(reinterpret_cast<intptr_t>(static_cast<VkPipelineCache>(pipelineCache)));
        }
    }

    PassInfo::PassInfo()
        : pVertShader{},
          pFragShader{},
          pCompShader{},
          topology{vk::PrimitiveTopology::eTriangleList},
          polygonMode{vk::PolygonMode::eFill},
          cullMode{vk::CullModeFlagBits::eBack},
          lineWidth{1.0f},
          blendEnable{false},
          colorBlendAttachments{},
          dynamicStates{},
          colorFormats{},
          depthFormat{vk::Format::eUndefined},
          depthWriteEnable{true},
          depthTestEnable{true},
          depthCompareOp{Settings::Get<GlobalSettings>().reverse_depth ? vk::CompareOp::eGreaterOrEqual : vk::CompareOp::eLessOrEqual},
          pipelineCache{},
          stencilTestEnable{false},
          stencilFailOp{vk::StencilOp::eKeep},
          stencilPassOp{vk::StencilOp::eReplace},
          stencilDepthFailOp{vk::StencilOp::eKeep},
          stencilCompareOp{vk::CompareOp::eAlways},
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
            vk::ShaderModuleCreateInfo csmci{};
            csmci.codeSize = info.pCompShader->BytesCount();
            csmci.pCode = info.pCompShader->GetSpriv();

            vk::PushConstantRange pcr{};
            const PushConstantDesc &pushConstantComp = info.pCompShader->GetPushConstantDesc();
            if (pushConstantComp.size > 0)
            {
                pcr.size = static_cast<uint32_t>(pushConstantComp.size);
                pcr.offset = 0;
                pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;
                PE_ERROR_IF(pcr.size > 128, "Compute push constant size is greater than 128 bytes");
                info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eCompute);
                info.m_pushConstantOffsets.push_back(pcr.offset);
                info.m_pushConstantSizes.push_back(pcr.size);
            }

            std::vector<vk::DescriptorSetLayout> layouts{};
            const auto &descriptors = info.GetDescriptors(0);
            for (uint32_t i = 0; i < descriptors.size(); i++)
                layouts.push_back(descriptors[i]->GetLayout()->ApiHandle());

            vk::PipelineLayoutCreateInfo plci{};
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();
            plci.pushConstantRangeCount = pcr.size ? 1 : 0;
            plci.pPushConstantRanges = pcr.size ? &pcr : nullptr;

            vk::ShaderModule module = RHII.GetDevice().createShaderModule(csmci);

            vk::ComputePipelineCreateInfo compinfo{};
            compinfo.stage.module = module;
            compinfo.stage.pName = info.pCompShader->GetEntryName().c_str();
            compinfo.stage.stage = vk::ShaderStageFlagBits::eCompute;

            m_layout = RHII.GetDevice().createPipelineLayout(plci);
            compinfo.layout = m_layout;

            auto result = RHII.GetDevice().createComputePipeline(m_cache, compinfo);
            if (result.result == vk::Result::eSuccess)
            {
                m_apiHandle = result.value;
            }
            else
            {
                PE_ERROR("Failed to create compute pipeline!");
            }

            RHII.GetDevice().destroyShaderModule(module);
        }
        else
        {
            vk::GraphicsPipelineCreateInfo pipeinfo{};

            vk::ShaderModuleCreateInfo vsmci{};
            vsmci.codeSize = info.pVertShader->BytesCount();
            vsmci.pCode = info.pVertShader->GetSpriv();

            vk::ShaderModule vertModule = RHII.GetDevice().createShaderModule(vsmci);

            vk::PipelineShaderStageCreateInfo pssci1{};
            pssci1.stage = vk::ShaderStageFlagBits::eVertex;
            pssci1.module = vertModule;
            pssci1.pName = info.pVertShader->GetEntryName().c_str();

            vk::ShaderModuleCreateInfo fsmci{};
            vk::ShaderModule fragModule;
            vk::PipelineShaderStageCreateInfo pssci2{};
            if (info.pFragShader)
            {
                fsmci.codeSize = info.pFragShader->BytesCount();
                fsmci.pCode = info.pFragShader->GetSpriv();
                fragModule = RHII.GetDevice().createShaderModule(fsmci);

                pssci2.stage = vk::ShaderStageFlagBits::eFragment;
                pssci2.module = fragModule;
                pssci2.pName = info.pFragShader->GetEntryName().c_str();
            }

            std::vector<vk::PipelineShaderStageCreateInfo> stages{pssci1};
            if (info.pFragShader)
                stages.push_back(pssci2);

            pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
            pipeinfo.pStages = stages.data();

            // Vertex Input state
            std::vector<vk::VertexInputBindingDescription> vibds = info.pVertShader->GetReflection().GetVertexBindings();
            std::vector<vk::VertexInputAttributeDescription> vidas = info.pVertShader->GetReflection().GetVertexAttributes();
            vk::PipelineVertexInputStateCreateInfo pvisci{};
            pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibds.size());
            pvisci.pVertexBindingDescriptions = vibds.data();
            pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(vidas.size());
            pvisci.pVertexAttributeDescriptions = vidas.data();
            pipeinfo.pVertexInputState = &pvisci;

            // Input Assembly stage
            vk::PipelineInputAssemblyStateCreateInfo piasci{};
            piasci.topology = info.topology;
            piasci.primitiveRestartEnable = VK_FALSE;
            pipeinfo.pInputAssemblyState = &piasci;

            // Dynamic states
            vk::PipelineDynamicStateCreateInfo dsi{};
            dsi.dynamicStateCount = static_cast<uint32_t>(info.dynamicStates.size());
            dsi.pDynamicStates = info.dynamicStates.data();
            pipeinfo.pDynamicState = &dsi;

            // Viewports and Scissors
            vk::PipelineViewportStateCreateInfo pvsci{};
            pvsci.viewportCount = 1;
            pvsci.scissorCount = 1;
            pipeinfo.pViewportState = &pvsci;

            // Rasterization state
            vk::PipelineRasterizationStateCreateInfo prsci{};
            prsci.depthClampEnable = VK_FALSE;
            prsci.rasterizerDiscardEnable = VK_FALSE;
            prsci.polygonMode = info.polygonMode;
            prsci.cullMode = info.cullMode;
            prsci.frontFace = vk::FrontFace::eClockwise;
            prsci.depthBiasEnable = VK_FALSE;
            prsci.depthBiasConstantFactor = 0.0f;
            prsci.depthBiasClamp = 0.0f;
            prsci.depthBiasSlopeFactor = 0.0f;
            prsci.lineWidth = max(info.lineWidth, 1.0f);
            pipeinfo.pRasterizationState = &prsci;

            // Multisample state
            vk::PipelineMultisampleStateCreateInfo pmsci{};
            pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
            pmsci.sampleShadingEnable = VK_FALSE;
            pmsci.minSampleShading = 1.0f;
            pmsci.pSampleMask = nullptr;
            pmsci.alphaToCoverageEnable = VK_FALSE;
            pmsci.alphaToOneEnable = VK_FALSE;
            pipeinfo.pMultisampleState = &pmsci;

            // Depth stencil state
            vk::PipelineDepthStencilStateCreateInfo pdssci{};
            pdssci.depthTestEnable = info.depthTestEnable;
            pdssci.depthWriteEnable = info.depthWriteEnable;
            pdssci.depthCompareOp = info.depthCompareOp;
            pdssci.stencilTestEnable = info.stencilTestEnable;
            pdssci.front.failOp = info.stencilFailOp;
            pdssci.front.passOp = info.stencilPassOp;
            pdssci.front.depthFailOp = info.stencilDepthFailOp;
            pdssci.front.compareOp = info.stencilCompareOp;
            pdssci.front.compareMask = info.stencilCompareMask;
            pdssci.front.writeMask = info.stencilWriteMask;
            pdssci.front.reference = info.stencilReference;
            pdssci.back = pdssci.front;
            pdssci.depthBoundsTestEnable = VK_FALSE;
            pdssci.minDepthBounds = 0.0f;
            pdssci.maxDepthBounds = 0.0f;
            pipeinfo.pDepthStencilState = &pdssci;

            // Color Blending state
            for (uint32_t i = 0; i < info.colorBlendAttachments.size(); i++)
            {
                info.colorBlendAttachments[i].blendEnable &= static_cast<vk::Bool32>(info.blendEnable);
            }

            vk::PipelineColorBlendStateCreateInfo pcbsci{};
            pcbsci.logicOpEnable = VK_FALSE;
            pcbsci.logicOp = vk::LogicOp::eAnd;
            pcbsci.attachmentCount = static_cast<uint32_t>(info.colorBlendAttachments.size());
            pcbsci.pAttachments = info.colorBlendAttachments.data();
            pcbsci.blendConstants[0] = 0.0f;
            pcbsci.blendConstants[1] = 0.0f;
            pcbsci.blendConstants[2] = 0.0f;
            pcbsci.blendConstants[3] = 0.0f;
            pipeinfo.pColorBlendState = &pcbsci;

            // Push Constant Range
            std::vector<vk::PushConstantRange> pcrs;
            pcrs.reserve(2);
            const PushConstantDesc &pushConstantVert = info.pVertShader ? info.pVertShader->GetPushConstantDesc() : PushConstantDesc{};
            const PushConstantDesc &pushConstantFrag = info.pFragShader ? info.pFragShader->GetPushConstantDesc() : PushConstantDesc{};
            if (pushConstantVert.size > 0 && pushConstantVert == pushConstantFrag)
            {
                vk::PushConstantRange vertPcr{};
                vertPcr.size = static_cast<uint32_t>(pushConstantVert.size);
                vertPcr.offset = 0;
                vertPcr.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
                pcrs.push_back(vertPcr);
                PE_ERROR_IF(vertPcr.size > RHII.GetMaxPushConstantsSize(), "Push constant size is greater than maxPushConstantsSize");

                info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
                info.m_pushConstantOffsets.push_back(vertPcr.offset);
                info.m_pushConstantSizes.push_back(vertPcr.size);
            }
            else
            {
                vk::PushConstantRange vertPcr{};
                vk::PushConstantRange fragPcr{};
                if (pushConstantVert.size > 0)
                {
                    vertPcr.size = static_cast<uint32_t>(pushConstantVert.size);
                    vertPcr.offset = 0;
                    vertPcr.stageFlags = vk::ShaderStageFlagBits::eVertex;
                    pcrs.push_back(vertPcr);
                    PE_ERROR_IF(vertPcr.size > RHII.GetMaxPushConstantsSize(), "Vertex push constant size is greater than maxPushConstantsSize");

                    info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eVertex);
                    info.m_pushConstantOffsets.push_back(vertPcr.offset);
                    info.m_pushConstantSizes.push_back(vertPcr.size);
                }
                if (pushConstantFrag.size > 0)
                {
                    fragPcr.size = static_cast<uint32_t>(pushConstantFrag.size);
                    fragPcr.offset = vertPcr.size;
                    fragPcr.stageFlags = vk::ShaderStageFlagBits::eFragment;
                    pcrs.push_back(fragPcr);
                    PE_ERROR_IF(fragPcr.offset + fragPcr.size > RHII.GetMaxPushConstantsSize(), "Fragment push constant size is greater than maxPushConstantsSize");

                    info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eFragment);
                    info.m_pushConstantOffsets.push_back(fragPcr.offset);
                    info.m_pushConstantSizes.push_back(fragPcr.size);
                }
            }

            // Pipeline Layout
            std::vector<vk::DescriptorSetLayout> layouts{};
            const auto &descriptors = info.GetDescriptors(0);
            for (uint32_t i = 0; i < descriptors.size(); i++)
                layouts.push_back(descriptors[i]->GetLayout()->ApiHandle());

            vk::PipelineLayoutCreateInfo plci{};
            plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
            plci.pSetLayouts = layouts.data();
            plci.pushConstantRangeCount = static_cast<uint32_t>(pcrs.size());
            plci.pPushConstantRanges = pcrs.data();

            m_layout = RHII.GetDevice().createPipelineLayout(plci);
            pipeinfo.layout = m_layout;

            // Render Pass
            uint32_t colorAttachmentCount = static_cast<uint32_t>(info.colorBlendAttachments.size());
            std::vector<vk::Format> vkFormats(colorAttachmentCount);
            vk::PipelineRenderingCreateInfo prci{};
            if (Settings::Get<GlobalSettings>().dynamic_rendering)
            {
                for (uint32_t i = 0; i < colorAttachmentCount; i++)
                {
                    vkFormats[i] = info.colorFormats[i];
                }

                prci.colorAttachmentCount = colorAttachmentCount;
                prci.pColorAttachmentFormats = vkFormats.data();

                if (HasDepth(info.depthFormat))
                {
                    prci.depthAttachmentFormat = info.depthFormat;
                    if (HasStencil(info.depthFormat))
                        prci.stencilAttachmentFormat = info.depthFormat;
                }

                pipeinfo.pNext = &prci;
                pipeinfo.renderPass = nullptr;
            }
            else
            {
                PE_ERROR_IF(!renderPass, "RenderPass is null");
                pipeinfo.renderPass = renderPass->ApiHandle();
            }

            // Subpass (Index of renderpass subpass this pipeline will be used in)
            pipeinfo.subpass = 0;

            // Base Pipeline
            pipeinfo.basePipelineHandle = nullptr;

            // Base Pipeline Index
            pipeinfo.basePipelineIndex = -1;

            vk::PipelineCacheCreateInfo cacheInfo{};
            cacheInfo.pInitialData = nullptr;
            cacheInfo.initialDataSize = 0;
            m_cache = RHII.GetDevice().createPipelineCache(cacheInfo);

            auto result = RHII.GetDevice().createGraphicsPipeline(m_cache, pipeinfo);
            if (result.result == vk::Result::eSuccess)
            {
                m_apiHandle = result.value;
            }
            else
            {
                PE_ERROR("Failed to create graphics pipeline!");
            }

            RHII.GetDevice().destroyShaderModule(vertModule);
            if (info.pFragShader && fragModule)
                RHII.GetDevice().destroyShaderModule(fragModule);

            Debug::SetObjectName(m_apiHandle, info.name);
        }
    }

    Pipeline::~Pipeline()
    {
        if (m_layout)
        {
            RHII.GetDevice().destroyPipelineLayout(m_layout);
            m_layout = vk::PipelineLayout{};
        }

        if (m_apiHandle)
        {
            RHII.GetDevice().destroyPipeline(m_apiHandle);
            m_apiHandle = vk::Pipeline{};
        }

        if (m_cache)
        {
            RHII.GetDevice().destroyPipelineCache(m_cache);
            m_cache = vk::PipelineCache{};
        }
    }
}
