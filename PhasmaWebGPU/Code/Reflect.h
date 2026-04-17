#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct WGPUDeviceImpl;
struct WGPUPipelineLayoutImpl;

namespace pwgpu
{
    struct AutoLayoutStageInput
    {
        const std::vector<uint32_t> *spirv = nullptr;
        std::string entryPoint;
        uint32_t executionModel = 0;
        WGPUShaderStage visibility = WGPUShaderStage_None;
    };

    using BindingKey = std::pair<uint32_t, uint32_t>; // (group, binding)

    struct LayoutCompatStageInput
    {
        const std::vector<uint32_t> *spirv = nullptr;
        std::string entryPoint;
        uint32_t executionModel = 0;
        WGPUShaderStage stage = WGPUShaderStage_None;
        // Null = legacy unfiltered behavior; set = WGSL-frontend filter.
        const std::set<BindingKey> *staticallyUsed = nullptr;
        const std::set<BindingKey> *comparisonSamplers = nullptr;
    };
    std::string ValidateExplicitLayoutCompat(const WGPUPipelineLayoutImpl *layout, const std::vector<LayoutCompatStageInput> &stages);

    WGPUPipelineLayoutImpl *BuildAutoPipelineLayout(WGPUDeviceImpl *device,
                                                    const std::vector<AutoLayoutStageInput> &stages,
                                                    std::string &errMsg);

    struct ComputeWorkgroupInfo
    {
        uint32_t sizeX = 1;
        uint32_t sizeY = 1;
        uint32_t sizeZ = 1;
        uint64_t workgroupStorageBytes = 0;
    };
    bool GetComputeWorkgroupInfo(const std::vector<uint32_t> &spirv,
                                 const std::string &entryPoint,
                                 ComputeWorkgroupInfo &out,
                                 std::string &errMsg);

    bool GetUsedDescriptorSets(const std::vector<uint32_t> &spirv,
                               const std::string &entryPoint,
                               uint32_t executionModel,
                               std::set<uint32_t> &outSets,
                               std::string &errMsg);

    enum class ShaderOutputBaseType
    {
        Float,
        Uint,
        Sint,
        Unknown
    };

    struct FragmentOutputInfo
    {
        ShaderOutputBaseType baseType = ShaderOutputBaseType::Unknown;
        uint32_t vecSize = 0;
    };
    bool GetFragmentOutputTypes(const std::vector<uint32_t> &spirv,
                                const std::string &entryPoint,
                                std::map<uint32_t, FragmentOutputInfo> &outInfos,
                                std::string &errMsg);

    struct VertexInputInfo
    {
        ShaderOutputBaseType baseType = ShaderOutputBaseType::Unknown;
        uint32_t vecSize = 0;
    };
    bool GetVertexInputTypes(const std::vector<uint32_t> &spirv,
                             const std::string &entryPoint,
                             std::map<uint32_t, VertexInputInfo> &outInfos,
                             std::string &errMsg);

    struct VaryingInfo
    {
        ShaderOutputBaseType baseType = ShaderOutputBaseType::Unknown;
        uint32_t vecSize = 0;
        bool isFlat = false;
        bool isNoPerspective = false;
        bool isCentroid = false;
        bool isSample = false;
    };
    bool GetVaryingInfos(const std::vector<uint32_t> &spirv,
                         const std::string &entryPoint,
                         uint32_t executionModel,
                         bool getInputs,
                         std::map<uint32_t, VaryingInfo> &outInfos,
                         std::string &errMsg);

    bool CountFragmentInterStageBuiltins(const std::vector<uint32_t> &spirv,
                                         const std::string &entryPoint,
                                         uint32_t &outCount,
                                         std::string &errMsg);

    struct FragmentOutputBuiltins
    {
        bool hasFragDepth = false;
        bool hasSampleMask = false;
    };
    bool GetFragmentOutputBuiltins(const std::vector<uint32_t> &spirv,
                                   const std::string &entryPoint,
                                   FragmentOutputBuiltins &out,
                                   std::string &errMsg);
} // namespace pwgpu
