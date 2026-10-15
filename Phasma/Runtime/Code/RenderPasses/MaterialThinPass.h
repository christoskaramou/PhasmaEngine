#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;

    // Material pass 1 (desktop OpenPBR): writes MaterialX's normal, albedo, roughness and metalness into the G-buffer
    // for visibility pixels of MaterialX materials, before SSAO/GI/reflections read it.
    class MaterialThinPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override {}
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_visibility = nullptr;
        Image *m_normal = nullptr;
        Image *m_albedo = nullptr;
        Image *m_srm = nullptr;
        Image *m_emissive = nullptr;
        ::PeFormat m_builtNormalFormat = PE_FORMAT_UNDEFINED;
        ::PeFormat m_builtEmissiveFormat = PE_FORMAT_UNDEFINED;
        bool m_bound = false; // this frame's descriptors were written: ExecutePass may dispatch
    };
} // namespace pe
