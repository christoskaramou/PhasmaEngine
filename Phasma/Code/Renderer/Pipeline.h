#pragma once

#include "Shader/Shader.h"
#include "Renderer/Vertex.h"

namespace pe
{
    class RenderPass;
    class DescriptorLayout;

    class PipelineColorBlendAttachmentState
    {
    public:
        PipelineColorBlendAttachmentState();

        uint32_t blendEnable;
        BlendFactor srcColorBlendFactor;
        BlendFactor dstColorBlendFactor;
        BlendOp colorBlendOp;
        BlendFactor srcAlphaBlendFactor;
        BlendFactor dstAlphaBlendFactor;
        BlendOp alphaBlendOp;
        ColorComponentFlags colorWriteMask;
    };

    class PassInfo : public Hashable, public NoCopy
    {
    public:
        PassInfo();

        ~PassInfo();

        void UpdateHash() override;

        const std::vector<Descriptor *> &GetDescriptors(uint32_t frame) const { return m_descriptors[frame]; }
        
        Shader *pVertShader;
        Shader *pFragShader;
        Shader *pCompShader;
        PrimitiveTopology topology;
        PolygonMode polygonMode;
        CullMode cullMode;
        float lineWidth;
        bool alphaBlend;
        std::vector<PipelineColorBlendAttachmentState> colorBlendAttachments;
        std::vector<DynamicState> dynamicStates;
        std::vector<Format> colorFormats;
        // Depth
        Format depthFormat;
        bool depthWriteEnable;
        bool depthTestEnable;
        CompareOp depthCompareOp;
        // Stencil
        bool stencilTestEnable;
        StencilOp stencilFailOp;
        StencilOp stencilPassOp;
        StencilOp stencilDepthFailOp;
        CompareOp stencilCompareOp;
        uint32_t stencilCompareMask;
        uint32_t stencilWriteMask;
        uint32_t stencilReference;

        PipelineCacheHandle pipelineCache;
        std::string name;

    private:
        friend class CommandBuffer;
        friend class Pipeline;
        friend class Shader;

        std::vector<ShaderStageFlags> m_pushConstantStages;
        std::vector<uint32_t> m_pushConstantOffsets;
        std::vector<uint32_t> m_pushConstantSizes;
        std::vector<Descriptor *> m_descriptors[SWAPCHAIN_IMAGES];
    };

    class Pipeline : public IHandle<Pipeline, PipelineHandle>
    {
    public:
        Pipeline(RenderPass* renderPass, PassInfo &info);

        ~Pipeline();

    private:
        friend class CommandBuffer;

        PassInfo &m_info;
        PipelineLayoutHandle m_layout;
    };
}