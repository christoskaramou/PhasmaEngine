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
        Pipeline *pipeline;
        std::shared_ptr<PipelineCreateInfo> pipelineInfo;
        Descriptor *DSet;

        Image *input;
        Image *output;
        Image *output5;
        uint32_t counter[6];

        struct PushConstants
        {
            uint32_t mips;
            uint32_t numWorkGroups;
            ivec2 workGroupOffset;
            vec2 invInputSize;
        } pushConstants;
    };
}
