#pragma once

namespace pe
{
    class Downsampler : public IRenderComponent
    {
    public:
        Downsampler();

        ~Downsampler();

        void Init() override;

        void UpdatePipelineInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

    private:
        Pipeline *m_pipeline;
        std::shared_ptr<PipelineCreateInfo> m_pipelineInfo;
        Descriptor *m_DSet;

        uint32_t m_counter[6];
        
        // Shader data
        Image *m_input;
        Image *m_output;
        Image *m_output5;
        Buffer *m_buffer;
        struct PushConstants
        {
            uint32_t mips;
            uint32_t numWorkGroups;
            ivec2 workGroupOffset;
            vec2 invInputSize;
        } m_pushConstants;
    };
}
