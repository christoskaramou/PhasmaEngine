#pragma once

#include <webgpu/webgpu.h>
#include "API/Shader.h"

struct WGPUShaderModuleImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Shader *shader = nullptr;
    std::vector<uint32_t> spirv;
};
