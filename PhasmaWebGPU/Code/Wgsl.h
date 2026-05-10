#pragma once

#include <string>
#include <vector>
#include <webgpu/webgpu.h>

#include "Export.h"
#include "ShaderModule.h"

struct NagaCompileResult;

namespace pwgpu::Wgsl
{
    class PWGPU_API CompileResult;

    PWGPU_API CompileResult Compile(WGPUStringView source);
    PWGPU_API CompileResult Compile(const std::string &source);

    PWGPU_API std::vector<uint32_t> BakeWithConstants(const std::string &source,
                                                      size_t constantCount,
                                                      const WGPUConstantEntry *constants,
                                                      const std::string &entryPoint);
    PWGPU_API std::string SpirvToHlsl(const std::vector<uint32_t> &spirv,
                                      const std::string &entryPoint,
                                      const char *stage);

    PWGPU_API void PopulateReflectionMeta(WGPUShaderModuleImpl *module,
                                          const CompileResult &result);
    PWGPU_API void ConvertMessages(const CompileResult &result,
                                   std::vector<WGPUCompilationMessageStorage> &messages);

    class PWGPU_API CompileResult
    {
    public:
        CompileResult() = default;
        explicit CompileResult(NagaCompileResult *result);
        ~CompileResult();

        CompileResult(const CompileResult &) = delete;
        CompileResult &operator=(const CompileResult &) = delete;

        CompileResult(CompileResult &&other) noexcept;
        CompileResult &operator=(CompileResult &&other) noexcept;

        const uint32_t *spirv() const;
        size_t spirvWordCount() const;
        bool valid() const { return m_result != nullptr; }

    private:
        NagaCompileResult *m_result = nullptr;

        friend void PopulateReflectionMeta(WGPUShaderModuleImpl *module,
                                           const CompileResult &result);
        friend void ConvertMessages(const CompileResult &result,
                                    std::vector<WGPUCompilationMessageStorage> &messages);
    };

} // namespace pwgpu::Wgsl
