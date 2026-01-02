#pragma once

struct FfxFsr2Context;
struct FfxFsr2ContextDescription;
struct FfxFsr2DispatchDescription;

namespace pe
{
    class CommandBuffer;
    class Image;

    struct SRSettings : public Settings
    {
        bool enable = false;
        vec2 motionScale = vec2(1.0f);
        vec2 projScale = vec2(1.0f);
        bool fsr2Debug = true;
    };

    class SuperResolutionPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void GenerateJitter();
        const vec2 &GetJitter() { return m_jitter; }
        const vec2 &GetProjectionJitter() { return m_projectionJitter; }

        Image *GetOutput() { return m_display; }

    private:
        std::shared_ptr<FfxFsr2Context> m_context;
        std::shared_ptr<FfxFsr2ContextDescription> m_contextDescription;
        Image *m_display;
        Image *m_viewportRT;
        Image *m_velocityRT;
        Image *m_transpanencyRT;
        Image *m_depthStencil;
        vec2 m_jitter;
        vec2 m_projectionJitter;
    };
} // namespace pe
