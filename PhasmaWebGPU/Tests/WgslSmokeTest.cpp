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
#endif
    return 0;
}
