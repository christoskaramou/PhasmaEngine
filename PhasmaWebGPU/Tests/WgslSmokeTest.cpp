#include "Wgsl.h"

#include <SDL.h>

int main(int /*argc*/, char * /*argv*/[])
{
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
    const std::string source =
        "@vertex fn vs() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0); }";
    auto result = pwgpu::Wgsl::Compile(source);
    if (!result.spirv() || result.spirvWordCount() < 5)
        return 1;

    WGPUShaderModuleImpl module{};
    pwgpu::Wgsl::PopulateReflectionMeta(&module, result);
    if (module.entryPoints.size() != 1)
        return 2;
    if (module.entryPoints[0].name != "vs" || module.entryPoints[0].executionModel != 0)
        return 3;

    const std::string uniformSource = R"(
struct U { value: f32, }
@group(0) @binding(0) var<uniform> u: U;
@vertex fn vs_uniform() -> @builtin(position) vec4<f32> {
    return vec4<f32>(u.value, 0.0, 0.0, 1.0);
})";
    auto uniformResult = pwgpu::Wgsl::Compile(uniformSource);
    if (!uniformResult.spirv() || uniformResult.spirvWordCount() < 5)
        return 4;
    std::vector<uint32_t> uniformSpirv(uniformResult.spirv(),
                                       uniformResult.spirv() + uniformResult.spirvWordCount());
    const std::string uniformHlsl = pwgpu::Wgsl::SpirvToHlsl(uniformSpirv, "vs_uniform", "vertex");
    if (uniformHlsl.find("register(b1") == std::string::npos ||
        uniformHlsl.find("register(b0") != std::string::npos)
        return 5;

    const std::string matrixSource = R"(
struct U {
    m: mat4x4<f32>,
    v: vec4<f32>,
}
@group(0) @binding(0) var<uniform> u: U;
@vertex fn vs_matrix() -> @builtin(position) vec4<f32> {
    return u.m * u.v;
})";
    auto matrixResult = pwgpu::Wgsl::Compile(matrixSource);
    if (!matrixResult.spirv() || matrixResult.spirvWordCount() < 5)
        return 6;
    std::vector<uint32_t> matrixSpirv(matrixResult.spirv(),
                                      matrixResult.spirv() + matrixResult.spirvWordCount());
    const std::string matrixHlsl = pwgpu::Wgsl::SpirvToHlsl(matrixSpirv, "vs_matrix", "vertex");
    if (matrixHlsl.empty() || (matrixHlsl.find("row_major") == std::string::npos &&
                               matrixHlsl.find("column_major") == std::string::npos))
    {
        std::cerr << "matrix HLSL translation failed or omitted explicit matrix layout:\n"
                  << matrixHlsl << "\n";
        return 7;
    }

    const std::string overrideSource = R"(
@group(0) @binding(0) var<storage, read_write> out: array<u32>;
override VALUE: u32;
@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    out[id.x] = VALUE;
})";
    WGPUConstantEntry valueA{};
    valueA.key = {"VALUE", WGPU_STRLEN};
    valueA.value = 2.0;
    WGPUConstantEntry valueB = valueA;
    valueB.value = 7.0;
    const std::vector<uint32_t> bakedA =
        pwgpu::Wgsl::BakeWithConstants(overrideSource, 1, &valueA, "main");
    const std::vector<uint32_t> bakedB =
        pwgpu::Wgsl::BakeWithConstants(overrideSource, 1, &valueB, "main");
    if (bakedA.empty() || bakedB.empty())
        return 8;
    if (bakedA == bakedB)
        return 9;
#endif
    return 0;
}
