#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class Camera;

    class DOFPass : public IRenderPassComponent
    {
    public:
        DOFPass();
        ~DOFPass();

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
        
        Image *m_frameImage;
        Image *m_displayRT;
        Image *m_depth;
    };
}