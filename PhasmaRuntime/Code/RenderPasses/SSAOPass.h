#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;

    class SSAOPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override {};
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_ssaoRT;
        Image *m_normalRT;
        Image *m_depth;
        mat4 m_proj;
        mat4 m_normalsToView;
    };
} // namespace pe
