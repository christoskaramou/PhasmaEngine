#pragma once

namespace pe
{
    class Image;
    class Buffer;
    class Camera;
    class CommandBuffer;

    class SSRPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update(Camera *camera) override;
        void Draw(CommandBuffer * cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        friend class Renderer;

        mat4 m_reflectionInput[4];
        Buffer *m_UBReflection[SWAPCHAIN_IMAGES];
        Image *m_ssrRT;
        Image *m_viewportRT;
        Image *m_normalRT;
        Image *m_depth;
        Image *m_srmRT;
        Image *m_albedoRT;
        Image *m_frameImage;
    };
}