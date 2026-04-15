#pragma once

#include <webgpu/webgpu.h>
#include "API/Shader.h"

struct WGPUDeviceImpl;

struct WGPUCompilationMessageStorage
{
    WGPUCompilationMessageType type = WGPUCompilationMessageType_Error;
    std::string message;
    uint64_t lineNum = 0;
    uint64_t linePos = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct WGPUShaderModuleImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;

    std::vector<uint32_t> spirv;
    std::vector<WGPUCompilationMessageStorage> compilationMessages;

    struct EntryPoint
    {
        uint32_t executionModel = 0;
        std::string name;
    };
    std::vector<EntryPoint> entryPoints;
};

namespace pwgpu
{
    bool ParseSpirvEntryPoints(const uint32_t *code, size_t codeSize,
                               std::vector<WGPUShaderModuleImpl::EntryPoint> &out);
} // namespace pwgpu
