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

    class PassInfo : public Hashable
    {
    public:
        void UpdateHash() override;

        PassInfo();

        ~PassInfo();

        Shader *pVertShader;
        Shader *pFragShader;
        Shader *pCompShader;
        std::vector<VertexInputBindingDescription> vertexInputBindingDescriptions;
        std::vector<VertexInputAttributeDescription> vertexInputAttributeDescriptions;
        float width;
        float height;
        PrimitiveTopology topology;
        PolygonMode polygonMode;
        CullMode cullMode;
        std::vector<PipelineColorBlendAttachmentState> colorBlendAttachments;
        std::vector<DynamicState> dynamicStates;
        ShaderStageFlags pushConstantStage;
        uint32_t pushConstantSize;
        std::vector<DescriptorLayout *> descriptorSetLayouts;
        RenderPass *renderPass;
        uint32_t dynamicColorTargets;
        Format *colorFormats;
        Format *depthFormat;
        bool depthWriteEnable;
        PipelineCacheHandle pipelineCache;
        std::string name;
    };

    class Pipeline : public IHandle<Pipeline, PipelineHandle>
    {
    public:
        Pipeline(const PassInfo &info);

        ~Pipeline();

        PassInfo info;

        PipelineLayoutHandle layout;
    };
}