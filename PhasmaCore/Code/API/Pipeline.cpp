#include "API/Pipeline.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"
#include "API/Vulkan/VulkanShaderImpl.h"

namespace pe
{
    PE_API const BlendState BlendState::Default = BlendState(
        /*blendEnable*/ true,
        /*srcColorBlendFactor*/ PE_BLEND_FACTOR_SRC_ALPHA,
        /*dstColorBlendFactor*/ PE_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        /*colorBlendOp*/ PE_BLEND_OP_ADD,
        /*srcAlphaBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*dstAlphaBlendFactor*/ PE_BLEND_FACTOR_ZERO,
        /*alphaBlendOp*/ PE_BLEND_OP_ADD,
        /*colorWriteMask*/ PE_COLOR_COMPONENT_RGBA);

    PE_API const BlendState BlendState::AdditiveColor = BlendState(
        /*blendEnable*/ true,
        /*srcColorBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*dstColorBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*colorBlendOp*/ PE_BLEND_OP_ADD,
        /*srcAlphaBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*dstAlphaBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*alphaBlendOp*/ PE_BLEND_OP_ADD,
        /*colorWriteMask*/ PE_COLOR_COMPONENT_RGBA);

    PE_API const BlendState BlendState::TransparencyBlend = BlendState(
        /*blendEnable*/ true,
        /*srcColorBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*dstColorBlendFactor*/ PE_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        /*colorBlendOp*/ PE_BLEND_OP_ADD,
        /*srcAlphaBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*dstAlphaBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*alphaBlendOp*/ PE_BLEND_OP_ADD,
        /*colorWriteMask*/ PE_COLOR_COMPONENT_R);

    PE_API const BlendState BlendState::ParticlesBlend = BlendState(
        /*blendEnable*/ true,
        /*srcColorBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*dstColorBlendFactor*/ PE_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        /*colorBlendOp*/ PE_BLEND_OP_ADD,
        /*srcAlphaBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*dstAlphaBlendFactor*/ PE_BLEND_FACTOR_ONE,
        /*alphaBlendOp*/ PE_BLEND_OP_ADD,
        /*colorWriteMask*/ PE_COLOR_COMPONENT_RGBA);

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

    PassInfo::PassInfo()
        : pVertShader{},
          pFragShader{},
          pCompShader{},
          topology{PE_TOPOLOGY_TRIANGLE_LIST},
          polygonMode{PE_POLYGON_MODE_FILL},
          cullMode{PE_CULL_MODE_BACK},
          lineWidth{1.0f},
          blendEnable{false},
          colorBlendAttachments{},
          dynamicStates{},
          colorFormats{},
          depthFormat{PE_FORMAT_UNDEFINED},
          depthWriteEnable{true},
          depthTestEnable{true},
          depthCompareOp{Settings::Get<GlobalSettings>().reverse_depth ? PE_COMPARE_OP_GREATER_OR_EQUAL : PE_COMPARE_OP_LESS_OR_EQUAL},
          stencilTestEnable{false},
          stencilFailOp{PE_STENCIL_OP_KEEP},
          stencilPassOp{PE_STENCIL_OP_REPLACE},
          stencilDepthFailOp{PE_STENCIL_OP_KEEP},
          stencilCompareOp{PE_COMPARE_OP_ALWAYS},
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

    static PeCullMode ParseCullMode(const std::string &s)
    {
        if (s == "front")
            return PE_CULL_MODE_FRONT;
        if (s == "back")
            return PE_CULL_MODE_BACK;
        if (s == "none")
            return PE_CULL_MODE_NONE;
        if (s == "frontAndBack")
            return PE_CULL_MODE_FRONT_AND_BACK;
        return PE_CULL_MODE_FRONT;
    }

    static PeCompareOp ParseCompareOp(const std::string &s)
    {
        if (s == "less")
            return PE_COMPARE_OP_LESS;
        if (s == "equal")
            return PE_COMPARE_OP_EQUAL;
        if (s == "lessOrEqual")
            return PE_COMPARE_OP_LESS_OR_EQUAL;
        if (s == "greater")
            return PE_COMPARE_OP_GREATER;
        if (s == "greaterOrEqual")
            return PE_COMPARE_OP_GREATER_OR_EQUAL;
        if (s == "always")
            return PE_COMPARE_OP_ALWAYS;
        if (s == "never")
            return PE_COMPARE_OP_NEVER;
        if (s == "notEqual")
            return PE_COMPARE_OP_NOT_EQUAL;
        return PE_COMPARE_OP_LESS_OR_EQUAL;
    }

    static PeTopology ParseTopology(const std::string &s)
    {
        if (s == "triangleList")
            return PE_TOPOLOGY_TRIANGLE_LIST;
        if (s == "lineList")
            return PE_TOPOLOGY_LINE_LIST;
        if (s == "lineStrip")
            return PE_TOPOLOGY_LINE_STRIP;
        if (s == "pointList")
            return PE_TOPOLOGY_POINT_LIST;
        if (s == "triangleStrip")
            return PE_TOPOLOGY_TRIANGLE_STRIP;
        if (s == "triangleFan")
            return PE_TOPOLOGY_TRIANGLE_FAN;
        return PE_TOPOLOGY_TRIANGLE_LIST;
    }

    static PePolygonMode ParsePolygonMode(const std::string &s)
    {
        if (s == "fill")
            return PE_POLYGON_MODE_FILL;
        if (s == "line")
            return PE_POLYGON_MODE_LINE;
        if (s == "point")
            return PE_POLYGON_MODE_POINT;
        return PE_POLYGON_MODE_FILL;
    }

    static PeStencilOp ParseStencilOp(const std::string &s)
    {
        if (s == "keep")
            return PE_STENCIL_OP_KEEP;
        if (s == "zero")
            return PE_STENCIL_OP_ZERO;
        if (s == "replace")
            return PE_STENCIL_OP_REPLACE;
        if (s == "incrementAndClamp")
            return PE_STENCIL_OP_INCREMENT_AND_CLAMP;
        if (s == "decrementAndClamp")
            return PE_STENCIL_OP_DECREMENT_AND_CLAMP;
        if (s == "invert")
            return PE_STENCIL_OP_INVERT;
        if (s == "incrementAndWrap")
            return PE_STENCIL_OP_INCREMENT_AND_WRAP;
        if (s == "decrementAndWrap")
            return PE_STENCIL_OP_DECREMENT_AND_WRAP;
        return PE_STENCIL_OP_KEEP;
    }

    static BlendState ParseBlendAttachment(const std::string &s)
    {
        if (s == "additive")
            return BlendState::AdditiveColor;
        if (s == "transparency")
            return BlendState::TransparencyBlend;
        if (s == "particles")
            return BlendState::ParticlesBlend;
        return BlendState::Default;
    }

    static PeDynamicState ParseDynamicState(const std::string &s)
    {
        if (s == "viewport")
            return PE_DYNAMIC_STATE_VIEWPORT;
        if (s == "scissor")
            return PE_DYNAMIC_STATE_SCISSOR;
        if (s == "lineWidth")
            return PE_DYNAMIC_STATE_LINE_WIDTH;
        if (s == "depthBias")
            return PE_DYNAMIC_STATE_DEPTH_BIAS;
        if (s == "blendConstants")
            return PE_DYNAMIC_STATE_BLEND_CONSTANTS;
        if (s == "stencilCompareMask")
            return PE_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
        if (s == "stencilWriteMask")
            return PE_DYNAMIC_STATE_STENCIL_WRITE_MASK;
        if (s == "stencilReference")
            return PE_DYNAMIC_STATE_STENCIL_REFERENCE;
        return PE_DYNAMIC_STATE_VIEWPORT;
    }

    static ::PeFormat ParseFormat(const std::string &s)
    {
        if (s == "r8g8b8a8Unorm")
            return PE_FORMAT_R8G8B8A8_UNORM;
        if (s == "r8g8b8a8Srgb")
            return PE_FORMAT_R8G8B8A8_SRGB;
        if (s == "b8g8r8a8Unorm")
            return PE_FORMAT_B8G8R8A8_UNORM;
        if (s == "r16g16b16a16Sfloat")
            return PE_FORMAT_R16G16B16A16_SFLOAT;
        if (s == "r16g16Sfloat")
            return PE_FORMAT_R16G16_SFLOAT;
        if (s == "r32g32b32a32Sfloat")
            return PE_FORMAT_R32G32B32A32_SFLOAT;
        if (s == "r32Sfloat")
            return PE_FORMAT_R32_SFLOAT;
        if (s == "r8Unorm")
            return PE_FORMAT_R8_UNORM;
        if (s == "d32Sfloat")
            return PE_FORMAT_D32_SFLOAT;
        if (s == "d24UnormS8Uint")
            return PE_FORMAT_D24_UNORM_S8_UINT;
        if (s == "d32SfloatS8Uint")
            return PE_FORMAT_D32_SFLOAT_S8_UINT;
        return PE_FORMAT_UNDEFINED;
    }

    void PassInfo::Apply(const PassVariant &variant)
    {
        if (!variant.vertexShader.empty())
        {
            ShaderDesc desc{};
            desc.sourcePath = Path::Assets + variant.vertexShader;
            desc.entryPoint = "mainVS";
            desc.stage = PE_SHADER_STAGE_VERTEX;
            pVertShader = Shader::Create(desc);
        }
        if (!variant.fragmentShader.empty())
        {
            ShaderDesc desc{};
            desc.sourcePath = Path::Assets + variant.fragmentShader;
            desc.entryPoint = "mainPS";
            desc.stage = PE_SHADER_STAGE_FRAGMENT;
            pFragShader = Shader::Create(desc);
        }

        if (!variant.cullMode.empty())
            cullMode = ParseCullMode(variant.cullMode);
        if (!variant.depthCompareOp.empty())
            depthCompareOp = ParseCompareOp(variant.depthCompareOp);
        depthWriteEnable = variant.depthWriteEnable;
        depthTestEnable = variant.depthTestEnable;
        blendEnable = variant.blendEnable;

        if (!variant.topology.empty())
            topology = ParseTopology(variant.topology);
        if (!variant.polygonMode.empty())
            polygonMode = ParsePolygonMode(variant.polygonMode);
        lineWidth = variant.lineWidth;

        stencilTestEnable = variant.stencilTestEnable;
        if (!variant.stencilFailOp.empty())
            stencilFailOp = ParseStencilOp(variant.stencilFailOp);
        if (!variant.stencilPassOp.empty())
            stencilPassOp = ParseStencilOp(variant.stencilPassOp);
        if (!variant.stencilDepthFailOp.empty())
            stencilDepthFailOp = ParseStencilOp(variant.stencilDepthFailOp);
        if (!variant.stencilCompareOp.empty())
            stencilCompareOp = ParseCompareOp(variant.stencilCompareOp);
        stencilCompareMask = variant.stencilCompareMask;
        stencilWriteMask = variant.stencilWriteMask;
        stencilReference = variant.stencilReference;

        if (!variant.colorBlendAttachments.empty())
        {
            colorBlendAttachments.clear();
            for (const auto &att : variant.colorBlendAttachments)
                colorBlendAttachments.push_back(ParseBlendAttachment(att));
        }

        if (!variant.dynamicStates.empty())
        {
            dynamicStates.clear();
            for (const auto &ds : variant.dynamicStates)
                dynamicStates.push_back(ParseDynamicState(ds));
        }

        if (!variant.colorFormats.empty())
        {
            colorFormats.clear();
            for (const auto &cf : variant.colorFormats)
                colorFormats.push_back(ParseFormat(cf));
        }
        if (!variant.depthFormat.empty())
            depthFormat = ParseFormat(variant.depthFormat);
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
            VulkanRhi::Device().destroyPipelineLayout(m_layout);
            m_layout = vk::PipelineLayout{};
        }

        if (m_apiHandle)
        {
            VulkanRhi::Device().destroyPipeline(m_apiHandle);
            m_apiHandle = vk::Pipeline{};
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

    void Pipeline::CreateComputePipeline()
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
            m_info.m_pushConstantStages.push_back(vk::ShaderStageFlagBits::eCompute);
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
        m_apiHandle = result.value;

        VulkanRhi::Device().destroyShaderModule(module);

        Debug::SetObjectName(m_apiHandle, m_info.name);
    }

    void Pipeline::CreateGraphicsPipeline(RenderPass *renderPass)
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
        m_cache = VulkanRhi::Device().createPipelineCache(cacheInfo);

        auto result = VulkanRhi::Device().createGraphicsPipeline(m_cache, pipeinfo);
        PE_ERROR_IF(result.result != vk::Result::eSuccess, "Failed to create graphics pipeline!");
        m_apiHandle = result.value;

        if (vertModule)
            VulkanRhi::Device().destroyShaderModule(vertModule);
        if (fragModule)
            VulkanRhi::Device().destroyShaderModule(fragModule);

        Debug::SetObjectName(m_apiHandle, m_info.name);
    }

    void Pipeline::CreateRayTracingPipeline()
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
            m_info.m_pushConstantStages.push_back(pushConstantStages);
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
        m_apiHandle = result.value;

        // Create SBT must happen while modules are valid?
        // No, Handles are retrieved from pipeline. Modules can be destroyed after pipeline creation.
        // But for safety and consistency with previous logic which deferred destruction, let's create SBT here?
        // Wait, SBT needs buffer allocation which is instant.
        CreateSBT();

        for (auto &m : tempModules)
            VulkanRhi::Device().destroyShaderModule(m);

        Debug::SetObjectName(m_apiHandle, m_info.name);
    }

    void Pipeline::CreateSBT()
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

        std::vector<uint8_t> handles = VulkanRhi::Device().getRayTracingShaderGroupHandlesKHR<uint8_t>(m_apiHandle, 0, groupCount, groupCount * handleSize);

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
