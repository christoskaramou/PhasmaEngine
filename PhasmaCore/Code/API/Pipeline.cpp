#include "API/Pipeline.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Shader.h"

namespace pe
{
    vk::PipelineColorBlendAttachmentState PipelineColorBlendAttachmentState::Default = vk::PipelineColorBlendAttachmentState(
        /*.blendEnable            =*/VK_TRUE,
        /*.srcColorBlendFactor    =*/vk::BlendFactor::eSrcAlpha,
        /*.dstColorBlendFactor    =*/vk::BlendFactor::eOneMinusSrcAlpha,
        /*.colorBlendOp           =*/vk::BlendOp::eAdd,
        /*.srcAlphaBlendFactor    =*/vk::BlendFactor::eOne,
        /*.dstAlphaBlendFactor    =*/vk::BlendFactor::eZero,
        /*.alphaBlendOp           =*/vk::BlendOp::eAdd,
        /*.colorWriteMask         =*/vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    vk::PipelineColorBlendAttachmentState PipelineColorBlendAttachmentState::AdditiveColor = vk::PipelineColorBlendAttachmentState(
        /*.blendEnable            =*/VK_TRUE,
        /*.srcColorBlendFactor    =*/vk::BlendFactor::eOne,
        /*.dstColorBlendFactor    =*/vk::BlendFactor::eOne,
        /*.colorBlendOp           =*/vk::BlendOp::eAdd,
        /*.srcAlphaBlendFactor    =*/vk::BlendFactor::eOne,
        /*.dstAlphaBlendFactor    =*/vk::BlendFactor::eOne,
        /*.alphaBlendOp           =*/vk::BlendOp::eAdd,
        /*.colorWriteMask         =*/vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    vk::PipelineColorBlendAttachmentState PipelineColorBlendAttachmentState::TransparencyBlend = vk::PipelineColorBlendAttachmentState(
        /*.blendEnable            =*/VK_TRUE,
        /*.srcColorBlendFactor    =*/vk::BlendFactor::eOne,
        /*.dstColorBlendFactor    =*/vk::BlendFactor::eOneMinusSrcColor,
        /*.colorBlendOp           =*/vk::BlendOp::eAdd,
        /*.srcAlphaBlendFactor    =*/vk::BlendFactor::eOne,
        /*.dstAlphaBlendFactor    =*/vk::BlendFactor::eOne,
        /*.alphaBlendOp           =*/vk::BlendOp::eAdd,
        /*.colorWriteMask         =*/vk::ColorComponentFlagBits::eR);

    vk::PipelineColorBlendAttachmentState PipelineColorBlendAttachmentState::ParticlesBlend = vk::PipelineColorBlendAttachmentState(
        /*.blendEnable            =*/VK_TRUE,
        /*.srcColorBlendFactor    =*/vk::BlendFactor::eOne,
        /*.dstColorBlendFactor    =*/vk::BlendFactor::eOneMinusSrcColor,
        /*.colorBlendOp           =*/vk::BlendOp::eAdd,
        /*.srcAlphaBlendFactor    =*/vk::BlendFactor::eOne,
        /*.dstAlphaBlendFactor    =*/vk::BlendFactor::eOne,
        /*.alphaBlendOp           =*/vk::BlendOp::eAdd,
        /*.colorWriteMask         =*/vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

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
          stencilReference{0},
          acceleration{nullptr}
    {
        m_descriptorsPF.resize(RHII.GetSwapchainImageCount(), std::vector<Descriptor *>{});
    }

    PassInfo::~PassInfo()
    {
        Shader::Destroy(pCompShader);
        Shader::Destroy(pVertShader);
        Shader::Destroy(pFragShader);
        Shader::Destroy(acceleration.rayGen);
        for (auto *miss : acceleration.miss)
            Shader::Destroy(miss);
        for (auto &hitGroup : acceleration.hitGroups)
        {
            Shader::Destroy(hitGroup.closestHit);
            Shader::Destroy(hitGroup.anyHit);
            Shader::Destroy(hitGroup.intersection);
        }
        for (auto &descriptors : m_descriptorsPF)
        {
            for (auto &descriptor : descriptors)
                Descriptor::Destroy(descriptor);
        }
    }

    void PassInfo::Update()
    {
        ReflectDescriptors();
        UpdateHash();
    }

    void PassInfo::ReflectDescriptors()
    {
        for (auto &descriptors : m_descriptorsPF)
        {
            // Clean up existing descriptors
            for (auto &descriptor : descriptors)
                Descriptor::Destroy(descriptor);

            descriptors = Shader::ReflectPassDescriptors(*this);
        }
    }

    void PassInfo::UpdateHash()
    {
        m_hash = {};

        for (auto &descriptors : m_descriptorsPF)
        {
            for (auto *descriptor : descriptors)
            {
                if (descriptor)
                {
                    m_hash.Combine(reinterpret_cast<intptr_t>(descriptor->GetLayout()));
                }
            }
        }

        if (acceleration.rayGen)
        {
            if (acceleration.rayGen)
            {
                m_hash.Combine(acceleration.rayGen->GetCache().GetHash());
            }
            for (auto *miss : acceleration.miss)
            {
                if (miss)
                {
                    m_hash.Combine(miss->GetCache().GetHash());
                }
            }
            for (auto &hitGroup : acceleration.hitGroups)
            {
                if (hitGroup.closestHit)
                {
                    m_hash.Combine(hitGroup.closestHit->GetCache().GetHash());
                }
                if (hitGroup.anyHit)
                {
                    m_hash.Combine(hitGroup.anyHit->GetCache().GetHash());
                }
                if (hitGroup.intersection)
                {
                    m_hash.Combine(hitGroup.intersection->GetCache().GetHash());
                }
            }
            m_hash.Combine(acceleration.maxRecursionDepth);
        }
        else if (pCompShader)
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

    Pipeline::Pipeline(RenderPass *renderPass, PassInfo &info) : m_info(info)
    {
        if (info.pCompShader)
        {
            CreateComputePipeline();
        }
        else if (info.acceleration.rayGen)
        {
            CreateRayTracingPipeline();
        }
        else
        {
            CreateGraphicsPipeline(renderPass);
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

        if (m_sbtBuffer)
        {
            Buffer::Destroy(m_sbtBuffer);
            m_sbtBuffer = nullptr;
        }
    }

    void Pipeline::CreateComputePipeline()
    {
        vk::ShaderModuleCreateInfo csmci{};
        csmci.codeSize = m_info.pCompShader->BytesCount();
        csmci.pCode = m_info.pCompShader->GetSpriv();

        vk::PushConstantRange pcr{};
        const PushConstantDesc &pushConstantComp = m_info.pCompShader->GetPushConstantDesc();
        if (pushConstantComp.size > 0)
        {
            pcr.size = static_cast<uint32_t>(pushConstantComp.size);
            pcr.offset = 0;
            pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;
            PE_ERROR_IF(pcr.size > 128, "Compute push constant size is greater than 128 bytes");
            m_info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eCompute);
            m_info.m_pushConstantOffsets.push_back(pcr.offset);
            m_info.m_pushConstantSizes.push_back(pcr.size);
        }

        std::vector<vk::DescriptorSetLayout> layouts{};
        const auto &descriptors = m_info.GetDescriptors(0);
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
        compinfo.stage.pName = m_info.pCompShader->GetEntryName().c_str();
        compinfo.stage.stage = vk::ShaderStageFlagBits::eCompute;

        m_layout = RHII.GetDevice().createPipelineLayout(plci);
        compinfo.layout = m_layout;

        auto result = RHII.GetDevice().createComputePipeline(m_cache, compinfo);
        PE_ERROR_IF(result.result != vk::Result::eSuccess, "Failed to create compute pipeline!");
        m_apiHandle = result.value;

        RHII.GetDevice().destroyShaderModule(module);

        Debug::SetObjectName(m_apiHandle, m_info.name);
    }

    void Pipeline::CreateGraphicsPipeline(RenderPass *renderPass)
    {
        vk::GraphicsPipelineCreateInfo pipeinfo{};

        // Shader stages
        vk::ShaderModule vertModule;
        vk::ShaderModule fragModule;

        std::vector<vk::PipelineShaderStageCreateInfo> stages;
        stages.reserve(2);

        if (m_info.pVertShader)
        {
            vk::ShaderModuleCreateInfo smci{};
            smci.codeSize = m_info.pVertShader->BytesCount();
            smci.pCode = m_info.pVertShader->GetSpriv();
            vertModule = RHII.GetDevice().createShaderModule(smci);

            vk::PipelineShaderStageCreateInfo pssci{};
            pssci.stage = vk::ShaderStageFlagBits::eVertex;
            pssci.module = vertModule;
            pssci.pName = m_info.pVertShader->GetEntryName().c_str();
            stages.push_back(pssci);
        }
        if (m_info.pFragShader)
        {
            vk::ShaderModuleCreateInfo smci{};
            smci.codeSize = m_info.pFragShader->BytesCount();
            smci.pCode = m_info.pFragShader->GetSpriv();
            fragModule = RHII.GetDevice().createShaderModule(smci);

            vk::PipelineShaderStageCreateInfo pssci{};
            pssci.stage = vk::ShaderStageFlagBits::eFragment;
            pssci.module = fragModule;
            pssci.pName = m_info.pFragShader->GetEntryName().c_str();
            stages.push_back(pssci);
        }

        pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
        pipeinfo.pStages = stages.data();

        // Vertex Input state
        std::vector<vk::VertexInputBindingDescription> vibds = m_info.pVertShader->GetReflection().GetVertexBindings();
        std::vector<vk::VertexInputAttributeDescription> vidas = m_info.pVertShader->GetReflection().GetVertexAttributes();
        vk::PipelineVertexInputStateCreateInfo pvisci{};
        pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibds.size());
        pvisci.pVertexBindingDescriptions = vibds.data();
        pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(vidas.size());
        pvisci.pVertexAttributeDescriptions = vidas.data();
        pipeinfo.pVertexInputState = &pvisci;

        // Input Assembly stage
        vk::PipelineInputAssemblyStateCreateInfo piasci{};
        piasci.topology = m_info.topology;
        piasci.primitiveRestartEnable = VK_FALSE;
        pipeinfo.pInputAssemblyState = &piasci;

        // Dynamic states
        vk::PipelineDynamicStateCreateInfo dsi{};
        dsi.dynamicStateCount = static_cast<uint32_t>(m_info.dynamicStates.size());
        dsi.pDynamicStates = m_info.dynamicStates.data();
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
        prsci.polygonMode = m_info.polygonMode;
        prsci.cullMode = m_info.cullMode;
        prsci.frontFace = vk::FrontFace::eClockwise;
        prsci.depthBiasEnable = VK_FALSE;
        prsci.depthBiasConstantFactor = 0.0f;
        prsci.depthBiasClamp = 0.0f;
        prsci.depthBiasSlopeFactor = 0.0f;
        prsci.lineWidth = max(m_info.lineWidth, 1.0f);
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
        pdssci.depthTestEnable = m_info.depthTestEnable;
        pdssci.depthWriteEnable = m_info.depthWriteEnable;
        pdssci.depthCompareOp = m_info.depthCompareOp;
        pdssci.stencilTestEnable = m_info.stencilTestEnable;
        pdssci.front.failOp = m_info.stencilFailOp;
        pdssci.front.passOp = m_info.stencilPassOp;
        pdssci.front.depthFailOp = m_info.stencilDepthFailOp;
        pdssci.front.compareOp = m_info.stencilCompareOp;
        pdssci.front.compareMask = m_info.stencilCompareMask;
        pdssci.front.writeMask = m_info.stencilWriteMask;
        pdssci.front.reference = m_info.stencilReference;
        pdssci.back = pdssci.front;
        pdssci.depthBoundsTestEnable = VK_FALSE;
        pdssci.minDepthBounds = 0.0f;
        pdssci.maxDepthBounds = 0.0f;
        pipeinfo.pDepthStencilState = &pdssci;

        // Color Blending state
        for (uint32_t i = 0; i < m_info.colorBlendAttachments.size(); i++)
            m_info.colorBlendAttachments[i].blendEnable &= static_cast<vk::Bool32>(m_info.blendEnable);

        vk::PipelineColorBlendStateCreateInfo pcbsci{};
        pcbsci.logicOpEnable = VK_FALSE;
        pcbsci.logicOp = vk::LogicOp::eAnd;
        pcbsci.attachmentCount = static_cast<uint32_t>(m_info.colorBlendAttachments.size());
        pcbsci.pAttachments = m_info.colorBlendAttachments.data();
        pcbsci.blendConstants[0] = 0.0f;
        pcbsci.blendConstants[1] = 0.0f;
        pcbsci.blendConstants[2] = 0.0f;
        pcbsci.blendConstants[3] = 0.0f;
        pipeinfo.pColorBlendState = &pcbsci;

        // Push Constant Range
        std::vector<vk::PushConstantRange> pcrs;
        pcrs.reserve(2);
        const PushConstantDesc &pushConstantVert = m_info.pVertShader ? m_info.pVertShader->GetPushConstantDesc() : PushConstantDesc{};
        const PushConstantDesc &pushConstantFrag = m_info.pFragShader ? m_info.pFragShader->GetPushConstantDesc() : PushConstantDesc{};
        if (pushConstantVert.size > 0 && pushConstantVert == pushConstantFrag)
        {
            vk::PushConstantRange vertPcr{};
            vertPcr.size = static_cast<uint32_t>(pushConstantVert.size);
            vertPcr.offset = 0;
            vertPcr.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
            pcrs.push_back(vertPcr);
            PE_ERROR_IF(vertPcr.size > RHII.GetMaxPushConstantsSize(), "Push constant size is greater than maxPushConstantsSize");

            m_info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
            m_info.m_pushConstantOffsets.push_back(vertPcr.offset);
            m_info.m_pushConstantSizes.push_back(vertPcr.size);
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

                m_info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eVertex);
                m_info.m_pushConstantOffsets.push_back(vertPcr.offset);
                m_info.m_pushConstantSizes.push_back(vertPcr.size);
            }
            if (pushConstantFrag.size > 0)
            {
                fragPcr.size = static_cast<uint32_t>(pushConstantFrag.size);
                fragPcr.offset = vertPcr.size;
                fragPcr.stageFlags = vk::ShaderStageFlagBits::eFragment;
                pcrs.push_back(fragPcr);
                PE_ERROR_IF(fragPcr.offset + fragPcr.size > RHII.GetMaxPushConstantsSize(), "Fragment push constant size is greater than maxPushConstantsSize");

                m_info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eFragment);
                m_info.m_pushConstantOffsets.push_back(fragPcr.offset);
                m_info.m_pushConstantSizes.push_back(fragPcr.size);
            }
        }

        // Pipeline Layout
        std::vector<vk::DescriptorSetLayout> layouts{};
        const auto &descriptors = m_info.GetDescriptors(0);
        for (uint32_t i = 0; i < descriptors.size(); i++)
        {
            if (descriptors[i])
                layouts.push_back(descriptors[i]->GetLayout()->ApiHandle());
            else
                layouts.push_back(DescriptorLayout::GetOrCreate({}, vk::ShaderStageFlagBits::eAll)->ApiHandle());
        }

        vk::PipelineLayoutCreateInfo plci{};
        plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
        plci.pSetLayouts = layouts.data();
        plci.pushConstantRangeCount = static_cast<uint32_t>(pcrs.size());
        plci.pPushConstantRanges = pcrs.data();
        pipeinfo.layout = RHII.GetDevice().createPipelineLayout(plci);
        m_layout = pipeinfo.layout;

        // Render Pass
        vk::PipelineRenderingCreateInfo prci{};
        if (Settings::Get<GlobalSettings>().dynamic_rendering)
        {
            prci.colorAttachmentCount = static_cast<uint32_t>(m_info.colorBlendAttachments.size());
            prci.pColorAttachmentFormats = m_info.colorFormats.data();
            if (VulkanHelpers::HasDepth(m_info.depthFormat))
                prci.depthAttachmentFormat = m_info.depthFormat;
            if (VulkanHelpers::HasStencil(m_info.depthFormat))
                prci.stencilAttachmentFormat = m_info.depthFormat;

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
        PE_ERROR_IF(result.result != vk::Result::eSuccess, "Failed to create graphics pipeline!");
        m_apiHandle = result.value;

        if (vertModule)
            RHII.GetDevice().destroyShaderModule(vertModule);
        if (fragModule)
            RHII.GetDevice().destroyShaderModule(fragModule);

        Debug::SetObjectName(m_apiHandle, m_info.name);
    }

    void Pipeline::CreateRayTracingPipeline()
    {
        std::vector<vk::PipelineShaderStageCreateInfo> stages;
        std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups;

        uint32_t currentStageIndex = 0;
        std::vector<vk::ShaderModule> tempModules;

        // RayGen
        if (m_info.acceleration.rayGen)
        {
            vk::PipelineShaderStageCreateInfo stage{};
            stage.stage = vk::ShaderStageFlagBits::eRaygenKHR;
            stage.module = RHII.GetDevice().createShaderModule({{}, m_info.acceleration.rayGen->BytesCount(), m_info.acceleration.rayGen->GetSpriv()});
            stage.pName = m_info.acceleration.rayGen->GetEntryName().c_str();
            stages.push_back(stage);
            tempModules.push_back(stage.module);

            vk::RayTracingShaderGroupCreateInfoKHR group{};
            group.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
            group.generalShader = currentStageIndex++;
            group.closestHitShader = VK_SHADER_UNUSED_KHR;
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
            groups.push_back(group);
        }

        // Miss
        for (auto *shader : m_info.acceleration.miss)
        {
            vk::PipelineShaderStageCreateInfo stage{};
            stage.stage = vk::ShaderStageFlagBits::eMissKHR;
            stage.module = RHII.GetDevice().createShaderModule({{}, shader->BytesCount(), shader->GetSpriv()});
            stage.pName = shader->GetEntryName().c_str();
            stages.push_back(stage);
            tempModules.push_back(stage.module);

            vk::RayTracingShaderGroupCreateInfoKHR group{};
            group.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
            group.generalShader = currentStageIndex++;
            group.closestHitShader = VK_SHADER_UNUSED_KHR;
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
            groups.push_back(group);
        }

        // Hit Groups
        for (auto &hg : m_info.acceleration.hitGroups)
        {
            vk::RayTracingShaderGroupCreateInfoKHR group{};
            group.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
            group.generalShader = VK_SHADER_UNUSED_KHR;
            group.closestHitShader = VK_SHADER_UNUSED_KHR;
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;

            if (hg.closestHit)
            {
                vk::PipelineShaderStageCreateInfo stage{};
                stage.stage = vk::ShaderStageFlagBits::eClosestHitKHR;
                stage.module = RHII.GetDevice().createShaderModule({{}, hg.closestHit->BytesCount(), hg.closestHit->GetSpriv()});
                stage.pName = hg.closestHit->GetEntryName().c_str();
                stages.push_back(stage);
                tempModules.push_back(stage.module);
                group.closestHitShader = currentStageIndex++;
            }
            if (hg.anyHit)
            {
                vk::PipelineShaderStageCreateInfo stage{};
                stage.stage = vk::ShaderStageFlagBits::eAnyHitKHR;
                stage.module = RHII.GetDevice().createShaderModule({{}, hg.anyHit->BytesCount(), hg.anyHit->GetSpriv()});
                stage.pName = hg.anyHit->GetEntryName().c_str();
                stages.push_back(stage);
                tempModules.push_back(stage.module);
                group.anyHitShader = currentStageIndex++;
            }

            groups.push_back(group);
        }

        // Pipeline Layout
        std::vector<vk::DescriptorSetLayout> layouts;
        const auto &descriptors = m_info.GetDescriptors(0);
        for (auto *desc : descriptors)
            layouts.push_back(desc->GetLayout()->ApiHandle());

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        m_layout = RHII.GetDevice().createPipelineLayout(layoutInfo);

        vk::RayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = m_info.acceleration.maxRecursionDepth;
        pipelineInfo.layout = m_layout;

        auto result = RHII.GetDevice().createRayTracingPipelineKHR(nullptr, nullptr, pipelineInfo);
        PE_ERROR_IF(result.result != vk::Result::eSuccess, "Failed to create Ray Tracing pipeline!");
        m_apiHandle = result.value;

        // Create SBT must happen while modules are valid?
        // No, Handles are retrieved from pipeline. Modules can be destroyed after pipeline creation.
        // But for safety and consistency with previous logic which deferred destruction, let's create SBT here?
        // Wait, SBT needs buffer allocation which is instant.
        CreateSBT();

        for (auto &m : tempModules)
            RHII.GetDevice().destroyShaderModule(m);

        Debug::SetObjectName(m_apiHandle, m_info.name);
    }

    void Pipeline::CreateSBT()
    {
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
        vk::PhysicalDeviceProperties2 props2{};
        props2.pNext = &rtProps;
        RHII.GetGpu().getProperties2(&props2);

        uint32_t handleSize = rtProps.shaderGroupHandleSize;
        uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
        uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;

        auto alignedSize = [&](uint32_t value)
        {
            return (value + baseAlignment - 1) & ~(baseAlignment - 1);
        };

        uint32_t groupCount = 1 + static_cast<uint32_t>(m_info.acceleration.miss.size()) + static_cast<uint32_t>(m_info.acceleration.hitGroups.size());
        uint32_t sbtSize = groupCount * handleSize;

        uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

        m_rgenRegion.stride = alignedSize(handleSizeAligned);
        m_rgenRegion.size = m_rgenRegion.stride;

        m_missRegion.stride = handleSizeAligned;
        m_missRegion.size = alignedSize(static_cast<uint32_t>(m_info.acceleration.miss.size()) * handleSizeAligned);

        m_hitRegion.stride = handleSizeAligned;
        m_hitRegion.size = alignedSize(static_cast<uint32_t>(m_info.acceleration.hitGroups.size()) * handleSizeAligned);

        m_callRegion = vk::StridedDeviceAddressRegionKHR();

        vk::DeviceSize totalSize = m_rgenRegion.size + m_missRegion.size + m_hitRegion.size + m_callRegion.size;

        m_sbtBuffer = Buffer::Create(totalSize,
                                     vk::BufferUsageFlagBits2::eShaderBindingTableKHR | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                     m_info.name + "_SBT");

        m_rgenRegion.deviceAddress = m_sbtBuffer->GetDeviceAddress();
        m_missRegion.deviceAddress = m_rgenRegion.deviceAddress + m_rgenRegion.size;
        m_hitRegion.deviceAddress = m_missRegion.deviceAddress + m_missRegion.size;

        std::vector<uint8_t> handles = RHII.GetDevice().getRayTracingShaderGroupHandlesKHR<uint8_t>(m_apiHandle, 0, groupCount, groupCount * handleSize);

        m_sbtBuffer->Map();
        auto *data = static_cast<uint8_t *>(m_sbtBuffer->Data());

        uint32_t handleIdx = 0;
        memcpy(data, handles.data() + (handleIdx++ * handleSize), handleSize);

        auto *missData = data + m_rgenRegion.size;
        for (size_t i = 0; i < m_info.acceleration.miss.size(); i++)
        {
            memcpy(missData + i * handleSizeAligned, handles.data() + (handleIdx++ * handleSize), handleSize);
        }

        auto *hitData = missData + m_missRegion.size;
        for (size_t i = 0; i < m_info.acceleration.hitGroups.size(); i++)
        {
            memcpy(hitData + i * handleSizeAligned, handles.data() + (handleIdx++ * handleSize), handleSize);
        }

        m_sbtBuffer->Flush();
        m_sbtBuffer->Unmap();
    }

} // namespace pe
