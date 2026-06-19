#pragma once

#include <SDL.h>
#include <webgpu/webgpu.h>

namespace pwgpu::test
{
    struct SampleTiming
    {
        double deltaSeconds = 0.0;
        double elapsedSeconds = 0.0;
        double fps = 0.0;
    };

    struct SampleContext
    {
        SDL_Window *window = nullptr;
        WGPUInstance instance = nullptr;
        WGPUAdapter adapter = nullptr;
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPUSurface surface = nullptr;
        WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
        WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
        std::string presentModeName;
        std::string apiName;
        std::string gpuName;
        std::string exeDir;
        std::string shaderDir;
        uint32_t width = 0;
        uint32_t height = 0;
        SampleTiming timing{};
    };
} // namespace pwgpu::test
