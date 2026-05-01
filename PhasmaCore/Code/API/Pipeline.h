#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class RenderPass;
    class Descriptor;
    class Shader;
    class Buffer;

    struct PE_API BlendState : PeBlendAttachmentState
    {
        constexpr BlendState() = default;
        constexpr BlendState(
            bool blendEnable,
            PeBlendFactor srcColorBlendFactor,
            PeBlendFactor dstColorBlendFactor,
            PeBlendOp colorBlendOp,
            PeBlendFactor srcAlphaBlendFactor,
            PeBlendFactor dstAlphaBlendFactor,
            PeBlendOp alphaBlendOp,
            PeColorComponentFlags colorWriteMask)
            : PeBlendAttachmentState{
                  blendEnable,
                  srcColorBlendFactor,
                  dstColorBlendFactor,
                  colorBlendOp,
                  srcAlphaBlendFactor,
                  dstAlphaBlendFactor,
                  alphaBlendOp,
                  colorWriteMask}
        {
        }

        static const BlendState Default;
        static const BlendState AdditiveColor;
        static const BlendState TransparencyBlend;
        static const BlendState ParticlesBlend;
    };

    struct PassVariant
    {
        std::string vertexShader;
        std::string fragmentShader;

        std::string cullMode;
        std::string depthCompareOp;
        bool depthWriteEnable = true;
        bool depthTestEnable = true;
        bool blendEnable = false;

        std::string topology;
        std::string polygonMode;
        float lineWidth = 1.0f;

        bool stencilTestEnable = false;
        std::string stencilFailOp;
        std::string stencilPassOp;
        std::string stencilDepthFailOp;
        std::string stencilCompareOp;
        uint32_t stencilCompareMask = 0xFFu;
        uint32_t stencilWriteMask = 0xFFu;
        uint32_t stencilReference = 0u;

        std::vector<std::string> colorBlendAttachments;
        std::vector<std::string> dynamicStates;
        std::vector<std::string> colorFormats;
        std::string depthFormat;

        std::string materialBufferName;
        std::string materialAnnotation;

        bool HasShaders() const { return !vertexShader.empty() || !fragmentShader.empty(); }
    };

    struct HitGroup
    {
        Shader *closestHit = nullptr;
        Shader *anyHit = nullptr;
        Shader *intersection = nullptr;
    };

    struct Acceleration
    {
        Shader *rayGen = nullptr;
        std::vector<Shader *> miss;
        std::vector<HitGroup> hitGroups;
        uint32_t maxRecursionDepth = 1;
    };

    class PassInfo : public Hashable, public NoCopy
    {
    public:
        PassInfo();
        ~PassInfo();

        void Apply(const PassVariant &variant);
        void Update();
        const std::vector<Descriptor *> &GetDescriptors(uint32_t frame) const { return m_descriptorsPF[frame]; }

        Shader *pVertShader;
        Shader *pFragShader;
        Shader *pCompShader;
        PeTopology topology;
        PePolygonMode polygonMode;
        PeCullMode cullMode;
        float lineWidth;
        bool blendEnable;
        std::vector<BlendState> colorBlendAttachments;
        std::vector<PeDynamicState> dynamicStates;
        std::vector<::PeFormat> colorFormats;
        ::PeFormat depthFormat;
        bool depthWriteEnable;
        bool depthTestEnable;
        PeCompareOp depthCompareOp;
        bool stencilTestEnable;
        PeStencilOp stencilFailOp;
        PeStencilOp stencilPassOp;
        PeStencilOp stencilDepthFailOp;
        PeCompareOp stencilCompareOp;
        uint32_t stencilCompareMask;
        uint32_t stencilWriteMask;
        uint32_t stencilReference;
        Acceleration acceleration;
        std::string name;

    private:
        friend class CommandBuffer;
        friend class Pipeline;
        friend class Shader;

        void ReflectDescriptors();
        void UpdateHash() override;

        std::vector<vk::ShaderStageFlags> m_pushConstantStages;
        std::vector<uint32_t> m_pushConstantOffsets;
        std::vector<uint32_t> m_pushConstantSizes;
        std::vector<std::vector<Descriptor *>> m_descriptorsPF;
    };

    class Pipeline : public PeHandle<Pipeline, vk::Pipeline>
    {
    public:
        Pipeline(RenderPass *renderPass, PassInfo &info);
        ~Pipeline();

        PassInfo &GetInfo() { return m_info; }

    private:
        friend class CommandBuffer;

        void CreateRayTracingPipeline();
        void CreateComputePipeline();
        void CreateGraphicsPipeline(RenderPass *renderPass);
        void CreateSBT();

        PassInfo &m_info;
        vk::PipelineLayout m_layout;
        vk::PipelineCache m_cache;

        // Ray Tracing
        Buffer *m_sbtBuffer = nullptr;
        vk::StridedDeviceAddressRegionKHR m_rgenRegion{};
        vk::StridedDeviceAddressRegionKHR m_missRegion{};
        vk::StridedDeviceAddressRegionKHR m_hitRegion{};
        vk::StridedDeviceAddressRegionKHR m_callRegion{};
    };
} // namespace pe
