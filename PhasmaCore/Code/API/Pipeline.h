#pragma once

namespace pe
{
    class RenderPass;
    class DescriptorLayout;
    class Shader;

    struct PipelineColorBlendAttachmentState
    {
        static vk::PipelineColorBlendAttachmentState Default;
        static vk::PipelineColorBlendAttachmentState AdditiveColor;
        static vk::PipelineColorBlendAttachmentState TransparencyBlend;
    };

    class PassInfo : public Hashable, public NoCopy
    {
    public:
        PassInfo();
        ~PassInfo();

        void Update();
        const std::vector<Descriptor *> &GetDescriptors(uint32_t frame) const { return m_descriptorsPF[frame]; }

        Shader *pVertShader;
        Shader *pFragShader;
        Shader *pCompShader;
        vk::PrimitiveTopology topology;
        vk::PolygonMode polygonMode;
        vk::CullModeFlags cullMode;
        float lineWidth;
        bool blendEnable;
        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        std::vector<vk::DynamicState> dynamicStates;
        std::vector<vk::Format> colorFormats;
        // Depth
        vk::Format depthFormat;
        bool depthWriteEnable;
        bool depthTestEnable;
        vk::CompareOp depthCompareOp;
        // Stencil
        bool stencilTestEnable;
        vk::StencilOp stencilFailOp;
        vk::StencilOp stencilPassOp;
        vk::StencilOp stencilDepthFailOp;
        vk::CompareOp stencilCompareOp;
        uint32_t stencilCompareMask;
        uint32_t stencilWriteMask;
        uint32_t stencilReference;

        vk::PipelineCache pipelineCache;
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

    private:
        friend class CommandBuffer;

        PassInfo &m_info;
        vk::PipelineLayout m_layout;
        vk::PipelineCache m_cache;
    };
} // namespace pe
