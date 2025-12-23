#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class Scene;

    struct PushConstants_DepthPass
    {
        uint32_t jointsCount;
    };

    class DepthPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetScene(Scene *scene) { m_scene = scene; }
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        friend class Scene;

        Image *m_depthStencil;
        Scene *m_scene = nullptr;
    };
} // namespace pe
