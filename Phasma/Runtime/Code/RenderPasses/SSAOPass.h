#pragma once

namespace pe
{
    class CommandBuffer;
    class Buffer;
    class Image;
    class PassInfo;

    class SSAOPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override;

    private:
        void AcquireTargets();

        Image *m_ssaoRT = nullptr;
        Image *m_ssaoRawRT = nullptr;
        Image *m_normalRT = nullptr;
        Image *m_depth = nullptr;
        mat4 m_proj;
        mat4 m_invProj;
        mat4 m_normalsToView;
        std::shared_ptr<PassInfo> m_blurPassInfo;
        std::vector<Buffer *> m_uniforms;
    };
} // namespace pe
