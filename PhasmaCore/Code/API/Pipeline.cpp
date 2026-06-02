#include "API/Pipeline.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/Pipeline_Internal.h"
#include "API/Vulkan/VulkanPipelineImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12PipelineImpl.h"
#endif
#include "API/RenderPass.h"
#include "API/Shader.h"
namespace pe
{
    namespace
    {
        Pipeline::Type GetPipelineType(const PassInfo &info)
        {
            if (info.pCompShader)
                return Pipeline::Type::Compute;
            if (info.acceleration.rayGen)
                return Pipeline::Type::RayTracing;
            return Pipeline::Type::Graphics;
        }
    } // namespace

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
          depthBiasEnable{false},
          depthBiasConstantFactor{0.0f},
          depthBiasClamp{0.0f},
          depthBiasSlopeFactor{0.0f},
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
            m_hash.Combine(depthBiasEnable);
            m_hash.Combine(depthBiasConstantFactor);
            m_hash.Combine(depthBiasClamp);
            m_hash.Combine(depthBiasSlopeFactor);
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

    Pipeline::Pipeline(RenderPass *renderPass, PassInfo &info)
        : m_info(info),
          m_type(GetPipelineType(info))
    {
        m_impl = CreatePipelineImpl(this, renderPass, info);
        m_pushConstantStages = info.m_pushConstantStages;
        m_pushConstantOffsets = info.m_pushConstantOffsets;
        m_pushConstantSizes = info.m_pushConstantSizes;
    }

    Pipeline::~Pipeline()
    {
        delete m_impl;
        m_impl = nullptr;
    }

    Pipeline *Pipeline::Create(RenderPass *renderPass, PassInfo &info)
    {
        Pipeline *pipeline = new Pipeline(renderPass, info);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Pipeline), reinterpret_cast<void *>(pipeline));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object Pipeline created (Handle: %p)", reinterpret_cast<void *>(pipeline));
#endif
#endif
        return pipeline;
    }

    void Pipeline::Destroy(Pipeline *&pipeline)
    {
        if (!pipeline)
            return;

#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object Pipeline destroyed (Handle: %p)", reinterpret_cast<void *>(pipeline));
#endif
        delete pipeline;
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Untrack(typeid(Pipeline), reinterpret_cast<void *>(pipeline));
#endif
        pipeline = nullptr;
    }

    std::vector<Pipeline *> Pipeline::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Pipeline));
        std::vector<Pipeline *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Pipeline *>(p));
        return out;
#else
        return {};
#endif
    }

    Pipeline::Impl *CreatePipelineImpl(Pipeline *owner, RenderPass *renderPass, PassInfo &info)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            return new Dx12PipelineImpl(owner, renderPass, info);
#else
            PE_ERROR("[Pipeline] DX12 pipeline creation is Windows-only");
            return nullptr;
#endif
        }

        return new VulkanPipelineImpl(owner, renderPass, info);
    }
} // namespace pe
