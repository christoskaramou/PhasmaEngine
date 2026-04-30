#pragma once

#include "SampleBase.h"

#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <webgpu/webgpu.h>

namespace pwgpu::test
{
    struct SampleAppDesc
    {
        const char *sampleName = "";
        const char *windowTitle = "";
        uint32_t initialWidth = 1280;
        uint32_t initialHeight = 720;
        uint32_t windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN;
        WGPUTextureUsage surfaceUsage = WGPUTextureUsage_RenderAttachment;
        WGPUCompositeAlphaMode alphaMode = WGPUCompositeAlphaMode_Opaque;
        bool forcePresentMode = false;
        WGPUPresentMode preferredPresentMode = WGPUPresentMode_Fifo;
        bool showFpsInTitle = true;
        std::vector<WGPUFeatureName> requiredFeatures{};
        std::vector<WGPUTextureFormat> surfaceViewFormats{};
    };

    class SampleApp
    {
    public:
        SampleApp(SampleBase &module, SampleAppDesc desc);

        int Run(int argc, char *argv[]);

    private:
        bool InitCommon(int argc, char *argv[]);
        void PumpEvents(bool &running);
        void ReconfigureSurface(uint32_t width, uint32_t height);
        bool UpdateTiming();
        void UpdateTitle() const;
        void ShutdownCommon();

        SampleBase &m_module;
        SampleAppDesc m_desc;
        SampleContext m_ctx;
        Uint64 m_prevTicks = 0;
        Uint64 m_fpsBase = 0;
        Uint64 m_freq = 0;
        uint64_t m_frameCount = 0;
        uint64_t m_exitAfterFrames = 0;
        double m_exitAfterSeconds = 0.0;
        int m_fpsCount = 0;
    };
} // namespace pwgpu::test
