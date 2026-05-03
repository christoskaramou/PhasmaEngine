#include "API/Vulkan/VulkanPipelineImpl.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/Helpers_Vulkan.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanRenderPassImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"
#include "API/Vulkan/VulkanShaderImpl.h"

namespace pe
{
    static vk::BlendFactor ToVulkanBlendFactor(PeBlendFactor factor)
    {
        switch (factor)
        {
        case PE_BLEND_FACTOR_ZERO:
            return vk::BlendFactor::eZero;
        case PE_BLEND_FACTOR_ONE:
            return vk::BlendFactor::eOne;
        case PE_BLEND_FACTOR_SRC_ALPHA:
            return vk::BlendFactor::eSrcAlpha;
        case PE_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return vk::BlendFactor::eOneMinusSrcAlpha;
        case PE_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return vk::BlendFactor::eOneMinusSrcColor;
        default:
            return vk::BlendFactor::eOne;
        }
    }

    static vk::BlendOp ToVulkanBlendOp(PeBlendOp op)
    {
        switch (op)
        {
        case PE_BLEND_OP_ADD:
            return vk::BlendOp::eAdd;
        case PE_BLEND_OP_SUBTRACT:
            return vk::BlendOp::eSubtract;
        case PE_BLEND_OP_REVERSE_SUBTRACT:
            return vk::BlendOp::eReverseSubtract;
        case PE_BLEND_OP_MIN:
            return vk::BlendOp::eMin;
        case PE_BLEND_OP_MAX:
            return vk::BlendOp::eMax;
        default:
            return vk::BlendOp::eAdd;
        }
    }

    static vk::ColorComponentFlags ToVulkanColorWriteMask(PeColorComponentFlags mask)
    {
        vk::ColorComponentFlags result{};
        if (mask & PE_COLOR_COMPONENT_R)
            result |= vk::ColorComponentFlagBits::eR;
        if (mask & PE_COLOR_COMPONENT_G)
            result |= vk::ColorComponentFlagBits::eG;
        if (mask & PE_COLOR_COMPONENT_B)
            result |= vk::ColorComponentFlagBits::eB;
        if (mask & PE_COLOR_COMPONENT_A)
            result |= vk::ColorComponentFlagBits::eA;
        return result;
    }

    static vk::PipelineColorBlendAttachmentState ToVulkanBlendAttachment(const BlendState &state, bool globalBlendEnable)
    {
        vk::PipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = state.blendEnable && globalBlendEnable ? VK_TRUE : VK_FALSE;
        attachment.srcColorBlendFactor = ToVulkanBlendFactor(state.srcColorBlendFactor);
        attachment.dstColorBlendFactor = ToVulkanBlendFactor(state.dstColorBlendFactor);
        attachment.colorBlendOp = ToVulkanBlendOp(state.colorBlendOp);
        attachment.srcAlphaBlendFactor = ToVulkanBlendFactor(state.srcAlphaBlendFactor);
        attachment.dstAlphaBlendFactor = ToVulkanBlendFactor(state.dstAlphaBlendFactor);
        attachment.alphaBlendOp = ToVulkanBlendOp(state.alphaBlendOp);
        attachment.colorWriteMask = ToVulkanColorWriteMask(state.colorWriteMask);
        return attachment;
    }
    VulkanPipelineImpl::VulkanPipelineImpl(Pipeline *owner, RenderPass *renderPass, PassInfo &info) : m_owner{owner}, m_info(info)
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

    VulkanPipelineImpl::~VulkanPipelineImpl()
    {
        if (m_layout)
        {
            VulkanRhi::Device().destroyPipelineLayout(m_layout);
            m_layout = vk::PipelineLayout{};
        }

        if (m_pipeline)
        {
            VulkanRhi::Device().destroyPipeline(m_pipeline);
            m_pipeline = vk::Pipeline{};
        }

        if (m_cache)
        {
            VulkanRhi::Device().destroyPipelineCache(m_cache);
            m_cache = vk::PipelineCache{};
        }

        if (m_sbtBuffer)
        {
            Buffer::Destroy(m_sbtBuffer);
            m_sbtBuffer = nullptr;
        }
    }

    void VulkanPipelineImpl::CreateComputePipeline()
    {
        m_info.m_pushConstantStages.clear();
        m_info.m_pushConstantOffsets.clear();
        m_info.m_pushConstantSizes.clear();

        vk::ShaderModuleCreateInfo csmci{};
        csmci.codeSize = GetVulkanShaderSpirvSizeBytes(m_info.pCompShader);
        csmci.pCode = GetVulkanShaderSpirv(m_info.pCompShader);

        vk::PushConstantRange pcr{};
        const PushConstantDesc &pushConstantComp = m_info.pCompShader->GetPushConstantDesc();
        if (pushConstantComp.size > 0)
        {
            pcr.size = static_cast<uint32_t>(pushConstantComp.size);
            pcr.offset = 0;
            pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;
            PE_ERROR_IF(pcr.size > 128, "Compute push constant size is greater than 128 bytes");
            m_info.m_pushConstantStages.push_back(PE_SHADER_STAGE_COMPUTE);
            m_info.m_pushConstantOffsets.push_back(pcr.offset);
            m_info.m_pushConstantSizes.push_back(pcr.size);
        }

        std::vector<vk::DescriptorSetLayout> layouts{};
        const auto &descriptors = m_info.GetDescriptors(0);
        for (uint32_t i = 0; i < descriptors.size(); i++)
            layouts.push_back(pe::GetVulkanDescriptorLayout(descriptors[i]->GetLayout()));

        vk::PipelineLayoutCreateInfo plci{};
        plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
        plci.pSetLayouts = layouts.data();
        plci.pushConstantRangeCount = pcr.size ? 1 : 0;
        plci.pPushConstantRanges = pcr.size ? &pcr : nullptr;

        vk::ShaderModule module = VulkanRhi::Device().createShaderModule(csmci);
        vk::ComputePipelineCreateInfo compinfo{};
        compinfo.stage.module = module;
        compinfo.stage.pName = m_info.pCompShader->GetEntryName().c_str();
        compinfo.stage.stage = vk::ShaderStageFlagBits::eCompute;

        m_layout = VulkanRhi::Device().createPipelineLayout(plci);
        compinfo.layout = m_layout;

        auto result = VulkanRhi::Device().createComputePipeline(m_cache, compinfo);
        PE_ERROR_IF(result.result != vk::Result::eSuccess, "Failed to create compute pipeline!");
        m_pipeline = result.value;

        VulkanRhi::Device().destroyShaderModule(module);

        Debug::SetObjectName(m_pipeline, m_info.name);
    }

    void VulkanPipelineImpl::CreateGraphicsPipeline(RenderPass *renderPass)
    {
        m_info.m_pushConstantStages.clear();
        m_info.m_pushConstantOffsets.clear();
        m_info.m_pushConstantSizes.clear();

        vk::GraphicsPipelineCreateInfo pipeinfo{};

        // Shader stages
        vk::ShaderModule vertModule;
        vk::ShaderModule fragModule;

        std::vector<vk::PipelineShaderStageCreateInfo> stages;
        stages.reserve(2);

        if (m_info.pVertShader)
        {
            vk::ShaderModuleCreateInfo smci{};
            smci.codeSize = GetVulkanShaderSpirvSizeBytes(m_info.pVertShader);
            smci.pCode = GetVulkanShaderSpirv(m_info.pVertShader);
            vertModule = VulkanRhi::Device().createShaderModule(smci);

            vk::PipelineShaderStageCreateInfo pssci{};
            pssci.stage = vk::ShaderStageFlagBits::eVertex;
            pssci.module = vertModule;
            pssci.pName = m_info.pVertShader->GetEntryName().c_str();
            stages.push_back(pssci);
        }
        if (m_info.pFragShader)
        {
            vk::ShaderModuleCreateInfo smci{};
            smci.codeSize = GetVulkanShaderSpirvSizeBytes(m_info.pFragShader);
            smci.pCode = GetVulkanShaderSpirv(m_info.pFragShader);
            fragModule = VulkanRhi::Device().createShaderModule(smci);

            vk::PipelineShaderStageCreateInfo pssci{};
            pssci.stage = vk::ShaderStageFlagBits::eFragment;
            pssci.module = fragModule;
            pssci.pName = m_info.pFragShader->GetEntryName().c_str();
            stages.push_back(pssci);
        }

        pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
        pipeinfo.pStages = stages.data();

        // Vertex Input state
        const std::vector<VertexInputBinding> neutralBindings = m_info.pVertShader->GetReflection().GetVertexBindings();
        const std::vector<VertexInputAttribute> neutralAttribs = m_info.pVertShader->GetReflection().GetVertexAttributes();

        std::vector<vk::VertexInputBindingDescription> vibds;
        vibds.reserve(neutralBindings.size());
        for (const VertexInputBinding &b : neutralBindings)
        {
            vk::VertexInputBindingDescription d{};
            d.binding = b.binding;
            d.stride = b.stride;
            d.inputRate = vk::VertexInputRate::eVertex;
            vibds.push_back(d);
        }

        std::vector<vk::VertexInputAttributeDescription> vidas;
        vidas.reserve(neutralAttribs.size());
        for (const VertexInputAttribute &a : neutralAttribs)
        {
            vk::VertexInputAttributeDescription d{};
            d.location = a.location;
            d.binding = a.binding;
            d.format = ToVkFormat(a.format);
            d.offset = a.offset;
            vidas.push_back(d);
        }

        vk::PipelineVertexInputStateCreateInfo pvisci{};
        pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibds.size());
        pvisci.pVertexBindingDescriptions = vibds.data();
        pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(vidas.size());
        pvisci.pVertexAttributeDescriptions = vidas.data();
        pipeinfo.pVertexInputState = &pvisci;

        // Input Assembly stage
        vk::PipelineInputAssemblyStateCreateInfo piasci{};
        piasci.topology = ToVkTopology(m_info.topology);
        piasci.primitiveRestartEnable = VK_FALSE;
        pipeinfo.pInputAssemblyState = &piasci;

        // Dynamic states
        std::vector<vk::DynamicState> vkDynamicStates;
        vkDynamicStates.reserve(m_info.dynamicStates.size());
        for (PeDynamicState ds : m_info.dynamicStates)
            vkDynamicStates.push_back(ToVkDynamicState(ds));
        vk::PipelineDynamicStateCreateInfo dsi{};
        dsi.dynamicStateCount = static_cast<uint32_t>(vkDynamicStates.size());
        dsi.pDynamicStates = vkDynamicStates.data();
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
        prsci.polygonMode = ToVkPolygonMode(m_info.polygonMode);
        prsci.cullMode = ToVkCullMode(m_info.cullMode);
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
        pdssci.depthCompareOp = ToVkCompareOp(m_info.depthCompareOp);
        pdssci.stencilTestEnable = m_info.stencilTestEnable;
        pdssci.front.failOp = ToVkStencilOp(m_info.stencilFailOp);
        pdssci.front.passOp = ToVkStencilOp(m_info.stencilPassOp);
        pdssci.front.depthFailOp = ToVkStencilOp(m_info.stencilDepthFailOp);
        pdssci.front.compareOp = ToVkCompareOp(m_info.stencilCompareOp);
        pdssci.front.compareMask = m_info.stencilCompareMask;
        pdssci.front.writeMask = m_info.stencilWriteMask;
        pdssci.front.reference = m_info.stencilReference;
        pdssci.back = pdssci.front;
        pdssci.depthBoundsTestEnable = VK_FALSE;
        pdssci.minDepthBounds = 0.0f;
        pdssci.maxDepthBounds = 0.0f;
        pipeinfo.pDepthStencilState = &pdssci;

        // Color Blending state
        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        colorBlendAttachments.reserve(m_info.colorBlendAttachments.size());
        for (const auto &attachment : m_info.colorBlendAttachments)
            colorBlendAttachments.push_back(ToVulkanBlendAttachment(attachment, m_info.blendEnable));

        vk::PipelineColorBlendStateCreateInfo pcbsci{};
        pcbsci.logicOpEnable = VK_FALSE;
        pcbsci.logicOp = vk::LogicOp::eAnd;
        pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        pcbsci.pAttachments = colorBlendAttachments.data();
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

            m_info.m_pushConstantStages.push_back(PE_SHADER_STAGE_VERTEX | PE_SHADER_STAGE_FRAGMENT);
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

                m_info.m_pushConstantStages.push_back(PE_SHADER_STAGE_VERTEX);
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

                m_info.m_pushConstantStages.push_back(PE_SHADER_STAGE_FRAGMENT);
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
                layouts.push_back(pe::GetVulkanDescriptorLayout(descriptors[i]->GetLayout()));
            else
                layouts.push_back(pe::GetVulkanDescriptorLayout(DescriptorLayout::GetOrCreate({}, PE_SHADER_STAGE_ALL)));
        }

        vk::PipelineLayoutCreateInfo plci{};
        plci.setLayoutCount = static_cast<uint32_t>(layouts.size());
        plci.pSetLayouts = layouts.data();
        plci.pushConstantRangeCount = static_cast<uint32_t>(pcrs.size());
        plci.pPushConstantRanges = pcrs.data();
        pipeinfo.layout = VulkanRhi::Device().createPipelineLayout(plci);
        m_layout = pipeinfo.layout;

        // Render Pass
        vk::PipelineRenderingCreateInfo prci{};
        std::vector<vk::Format> vkColorFormats;
        if (Settings::Get<GlobalSettings>().dynamic_rendering)
        {
            vkColorFormats.reserve(m_info.colorFormats.size());
            for (::PeFormat f : m_info.colorFormats)
                vkColorFormats.push_back(ToVkFormat(f));

            const vk::Format vkDepthFormat = ToVkFormat(m_info.depthFormat);

            prci.colorAttachmentCount = static_cast<uint32_t>(m_info.colorBlendAttachments.size());
            prci.pColorAttachmentFormats = vkColorFormats.data();
            if (VulkanHelpers::HasDepth(vkDepthFormat))
                prci.depthAttachmentFormat = vkDepthFormat;
            if (VulkanHelpers::HasStencil(vkDepthFormat))
                prci.stencilAttachmentFormat = vkDepthFormat;

            pipeinfo.pNext = &prci;
            pipeinfo.renderPass = nullptr;
        }
        else
        {
            PE_ERROR_IF(!renderPass, "RenderPass is null");
            pipeinfo.renderPass = pe::GetVulkanRenderPass(renderPass);
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
        m_cache = VulkanRhi::Device().createPipelineCache(cacheInfo);

        auto result = VulkanRhi::Device().createGraphicsPipeline(m_cache, pipeinfo);
        PE_ERROR_IF(result.result != vk::Result::eSuccess, "Failed to create graphics pipeline!");
        m_pipeline = result.value;

        if (vertModule)
            VulkanRhi::Device().destroyShaderModule(vertModule);
        if (fragModule)
            VulkanRhi::Device().destroyShaderModule(fragModule);

        Debug::SetObjectName(m_pipeline, m_info.name);
    }

    void VulkanPipelineImpl::CreateRayTracingPipeline()
    {
        m_info.m_pushConstantStages.clear();
        m_info.m_pushConstantOffsets.clear();
        m_info.m_pushConstantSizes.clear();

        std::vector<vk::PipelineShaderStageCreateInfo> stages;
        std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups;

        uint32_t currentStageIndex = 0;
        std::vector<vk::ShaderModule> tempModules;

        // RayGen
        if (m_info.acceleration.rayGen)
        {
            vk::PipelineShaderStageCreateInfo stage{};
            stage.stage = vk::ShaderStageFlagBits::eRaygenKHR;
            stage.module = VulkanRhi::Device().createShaderModule({{}, GetVulkanShaderSpirvSizeBytes(m_info.acceleration.rayGen), GetVulkanShaderSpirv(m_info.acceleration.rayGen)});
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
            stage.module = VulkanRhi::Device().createShaderModule({{}, GetVulkanShaderSpirvSizeBytes(shader), GetVulkanShaderSpirv(shader)});
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
                stage.module = VulkanRhi::Device().createShaderModule({{}, GetVulkanShaderSpirvSizeBytes(hg.closestHit), GetVulkanShaderSpirv(hg.closestHit)});
                stage.pName = hg.closestHit->GetEntryName().c_str();
                stages.push_back(stage);
                tempModules.push_back(stage.module);
                group.closestHitShader = currentStageIndex++;
            }
            if (hg.anyHit)
            {
                vk::PipelineShaderStageCreateInfo stage{};
                stage.stage = vk::ShaderStageFlagBits::eAnyHitKHR;
                stage.module = VulkanRhi::Device().createShaderModule({{}, GetVulkanShaderSpirvSizeBytes(hg.anyHit), GetVulkanShaderSpirv(hg.anyHit)});
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
            layouts.push_back(pe::GetVulkanDescriptorLayout(desc->GetLayout()));

        // Push Constants
        std::vector<vk::PushConstantRange> pcrs{};
        uint32_t pushConstantSize = 0;
        vk::ShaderStageFlags pushConstantStages = {};

        auto checkShader = [&](Shader *shader, vk::ShaderStageFlagBits stage)
        {
            if (shader)
            {
                auto &desc = shader->GetPushConstantDesc();
                if (desc.size > 0)
                {
                    pushConstantSize = std::max(pushConstantSize, static_cast<uint32_t>(desc.size)); // Assumes same block setup
                    pushConstantStages |= stage;
                }
            }
        };

        checkShader(m_info.acceleration.rayGen, vk::ShaderStageFlagBits::eRaygenKHR);
        for (auto *shader : m_info.acceleration.miss)
            checkShader(shader, vk::ShaderStageFlagBits::eMissKHR);
        for (auto &hg : m_info.acceleration.hitGroups)
        {
            checkShader(hg.closestHit, vk::ShaderStageFlagBits::eClosestHitKHR);
            checkShader(hg.anyHit, vk::ShaderStageFlagBits::eAnyHitKHR);
            checkShader(hg.intersection, vk::ShaderStageFlagBits::eIntersectionKHR);
        }

        if (pushConstantSize > 0)
        {
            vk::PushConstantRange pcr{};
            pcr.stageFlags = pushConstantStages;
            pcr.offset = 0;
            pcr.size = pushConstantSize;
            pcrs.push_back(pcr);

            PE_ERROR_IF(pcr.size > RHII.GetMaxPushConstantsSize(), "RayTracing push constant size is greater than maxPushConstantsSize");
            m_info.m_pushConstantStages.push_back(FromVkShaderStageFlags(pushConstantStages));
            m_info.m_pushConstantOffsets.push_back(pcr.offset);
            m_info.m_pushConstantSizes.push_back(pcr.size);
        }

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pcrs.size());
        layoutInfo.pPushConstantRanges = pcrs.data();
        m_layout = VulkanRhi::Device().createPipelineLayout(layoutInfo);

        vk::RayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = m_info.acceleration.maxRecursionDepth;
        pipelineInfo.layout = m_layout;

        auto result = VulkanRhi::Device().createRayTracingPipelineKHR(nullptr, nullptr, pipelineInfo);
        PE_ERROR_IF(result.result != vk::Result::eSuccess, "Failed to create Ray Tracing pipeline!");
        m_pipeline = result.value;

        // Create SBT must happen while modules are valid?
        // No, Handles are retrieved from pipeline. Modules can be destroyed after pipeline creation.
        // But for safety and consistency with previous logic which deferred destruction, let's create SBT here?
        // Wait, SBT needs buffer allocation which is instant.
        CreateSBT();

        for (auto &m : tempModules)
            VulkanRhi::Device().destroyShaderModule(m);

        Debug::SetObjectName(m_pipeline, m_info.name);
    }

    void VulkanPipelineImpl::CreateSBT()
    {
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
        vk::PhysicalDeviceProperties2 props2{};
        props2.pNext = &rtProps;
        VulkanRhi::Gpu().getProperties2(&props2);

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

        m_sbtBuffer = Buffer::Create({
            .size = totalSize,
            .usage = PE_BUFFER_USAGE_SHADER_BINDING_TABLE_KHR | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = m_info.name + "_SBT",
        });

        m_rgenRegion.deviceAddress = m_sbtBuffer->GetDeviceAddress();
        m_missRegion.deviceAddress = m_rgenRegion.deviceAddress + m_rgenRegion.size;
        m_hitRegion.deviceAddress = m_missRegion.deviceAddress + m_missRegion.size;

        std::vector<uint8_t> handles = VulkanRhi::Device().getRayTracingShaderGroupHandlesKHR<uint8_t>(m_pipeline, 0, groupCount, groupCount * handleSize);

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
