#pragma once

#include <set>
#include <string>
#include <vector>
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

// Side-channel info the WGSL frontend provides that SPIR-V cannot express:
// sampler vs sampler_comparison, and statically-used bindings (naga drops
// phony-assignment references before SPIR-V codegen).
struct WGPUShaderReflectionMeta
{
    struct Binding
    {
        uint32_t group = 0;
        uint32_t binding = 0;
        std::string name;
        std::string kind;
        std::string format;
        std::string access;
    };
    struct EntryPoint
    {
        std::string name;
        std::string stage;
        std::vector<Binding> staticallyUsed;
    };
    // identifier = decimal(@id) if present, else name (§10.3.2.3).
    struct Override
    {
        std::string identifier;
        std::string name;
        std::string type;
        bool hasDefault = false;
        bool staticallyUsed = false;
        // Disambiguates "no EP uses this override" from "no metadata supplied"
        // (empty() alone is ambiguous; same pattern as
        // staticallyUsedAuthoritative in Reflect.h). false → fall back to
        // staticallyUsed bool. true → trust the set.
        bool staticallyUsedByEntryPointAuthoritative = false;
        std::set<std::string> staticallyUsedByEntryPoint;
    };
    std::vector<EntryPoint> entryPoints;
    std::vector<Binding> comparisonSamplers;
    std::vector<Override> overrides;
    bool present = false;
};

struct WGPUShaderModuleImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;

    std::vector<uint32_t> spirv;
    std::string wgslSource;
    std::vector<WGPUCompilationMessageStorage> compilationMessages;

    struct EntryPoint
    {
        uint32_t executionModel = 0;
        std::string name;
    };
    std::vector<EntryPoint> entryPoints;

    WGPUShaderReflectionMeta reflection;
};

namespace pwgpu
{
    bool ParseSpirvEntryPoints(const uint32_t *code, size_t codeSize,
                               std::vector<WGPUShaderModuleImpl::EntryPoint> &out);
} // namespace pwgpu
