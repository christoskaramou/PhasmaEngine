#include "Wgsl.h"
#include "Utils.h"

#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
#include "naga_bridge.h"
#endif

#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
namespace
{
    std::string CStringOrEmpty(const char *value)
    {
        return value ? std::string(value) : std::string();
    }

    WGPUShaderReflectionMeta::Binding ToBinding(const NagaBindingView &view)
    {
        WGPUShaderReflectionMeta::Binding binding{};
        binding.group = view.group;
        binding.binding = view.binding;
        binding.name = CStringOrEmpty(view.name);
        binding.kind = CStringOrEmpty(view.kind);
        binding.format = CStringOrEmpty(view.format);
        binding.access = CStringOrEmpty(view.access);
        return binding;
    }

    uint32_t ExecutionModelFromStage(const std::string &stage)
    {
        if (stage == "vertex")
            return 0;
        if (stage == "fragment")
            return 4;
        if (stage == "compute")
            return 5;
        return 0;
    }
} // namespace
#endif

namespace pwgpu::Wgsl
{
    CompileResult::CompileResult(NagaCompileResult *result)
        : m_result(result)
    {
    }

    CompileResult::~CompileResult()
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        if (m_result)
            naga_result_free(m_result);
#endif
    }

    CompileResult::CompileResult(CompileResult &&other) noexcept
        : m_result(other.m_result)
    {
        other.m_result = nullptr;
    }

    CompileResult &CompileResult::operator=(CompileResult &&other) noexcept
    {
        if (this == &other)
            return *this;
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        if (m_result)
            naga_result_free(m_result);
#endif
        m_result = other.m_result;
        other.m_result = nullptr;
        return *this;
    }

    const uint32_t *CompileResult::spirv() const
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        if (!m_result)
            return nullptr;
        size_t wordCount = 0;
        return naga_result_spirv(m_result, &wordCount);
#else
        return nullptr;
#endif
    }

    size_t CompileResult::spirvWordCount() const
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        if (!m_result)
            return 0;
        size_t wordCount = 0;
        naga_result_spirv(m_result, &wordCount);
        return wordCount;
#else
        return 0;
#endif
    }

    CompileResult Compile(WGPUStringView source)
    {
        return Compile(pwgpu::ToString(source));
    }

    CompileResult Compile(const std::string &source)
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        naga_install_panic_suppression();
        return CompileResult(naga_compile_wgsl(source.data(), source.size()));
#else
        return CompileResult();
#endif
    }

    std::vector<uint32_t> BakeWithConstants(const std::string &source,
                                            size_t constantCount,
                                            const WGPUConstantEntry *constants,
                                            const std::string &entryPoint)
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        std::vector<std::string> keyStorage;
        std::vector<NagaConstantKv> kvs;
        keyStorage.reserve(constantCount);
        kvs.reserve(constantCount);
        for (size_t i = 0; i < constantCount; ++i)
        {
            keyStorage.push_back(pwgpu::ToString(constants[i].key));
            kvs.push_back({keyStorage.back().c_str(), constants[i].value});
        }

        size_t wordCount = 0;
        const uint32_t *words = naga_bake_with_constants(
            source.data(), source.size(),
            kvs.empty() ? nullptr : kvs.data(), kvs.size(),
            entryPoint.empty() ? nullptr : entryPoint.c_str(),
            &wordCount);
        if (!words || wordCount == 0)
            return {};

        std::vector<uint32_t> out(words, words + wordCount);
        naga_bake_free(words);
        return out;
#else
        return {};
#endif
    }

    std::string SpirvToHlsl(const std::vector<uint32_t> &spirv,
                            const std::string &entryPoint,
                            const char *stage)
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        if (spirv.empty())
            return {};

        size_t byteCount = 0;
        const char *hlsl = naga_spirv_to_hlsl(
            spirv.data(), spirv.size(),
            entryPoint.empty() ? nullptr : entryPoint.c_str(),
            stage,
            &byteCount);
        if (!hlsl)
            return {};

        std::string out(hlsl, hlsl + byteCount);
        naga_string_free(hlsl);
        return out;
#else
        (void)spirv;
        (void)entryPoint;
        (void)stage;
        return {};
#endif
    }

    void PopulateReflectionMeta(WGPUShaderModuleImpl *module, const CompileResult &result)
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        if (!module || !result.m_result)
        {
            return;
        }

        module->reflection = WGPUShaderReflectionMeta{};
        module->reflection.present = true;

        const size_t comparisonSamplerCount =
            naga_result_comparison_sampler_count(result.m_result);
        module->reflection.comparisonSamplers.reserve(comparisonSamplerCount);
        for (size_t i = 0; i < comparisonSamplerCount; ++i)
        {
            NagaBindingView view{};
            naga_result_comparison_sampler(result.m_result, i, &view);
            module->reflection.comparisonSamplers.push_back(ToBinding(view));
        }

        const size_t entryPointCount = naga_result_entry_point_count(result.m_result);
        module->reflection.entryPoints.reserve(entryPointCount);
        if (entryPointCount > 0)
            module->entryPoints.clear();
        for (size_t i = 0; i < entryPointCount; ++i)
        {
            NagaEntryPointView view{};
            naga_result_entry_point(result.m_result, i, &view);

            WGPUShaderReflectionMeta::EntryPoint entryPoint{};
            entryPoint.name = CStringOrEmpty(view.name);
            entryPoint.stage = CStringOrEmpty(view.stage);
            entryPoint.staticallyUsed.reserve(view.statically_used_count);
            for (size_t b = 0; b < view.statically_used_count; ++b)
                entryPoint.staticallyUsed.push_back(ToBinding(view.statically_used[b]));

            WGPUShaderModuleImpl::EntryPoint moduleEntryPoint{};
            moduleEntryPoint.name = entryPoint.name;
            moduleEntryPoint.executionModel = ExecutionModelFromStage(entryPoint.stage);
            module->entryPoints.push_back(std::move(moduleEntryPoint));
            module->reflection.entryPoints.push_back(std::move(entryPoint));
        }

        // Build the known-EP set from the same compile result. Per the
        // 2026-04-26 codex-review-response: empty-as-sentinel for the per-EP
        // attribution is the wrong default. If naga's text-scan ever misses
        // an EP body (extract_fn_body fails on unusual function syntax), the
        // override would be silently treated as not-required for that EP.
        // Only mark the per-EP set authoritative when every supplied name is
        // known; otherwise fall back to the module-wide staticallyUsed bool.
        std::set<std::string> knownEps;
        for (const auto &ep : module->reflection.entryPoints)
            knownEps.insert(ep.name);
        for (const auto &ep : module->entryPoints)
            knownEps.insert(ep.name);

        const size_t overrideCount = naga_result_override_count(result.m_result);
        module->reflection.overrides.reserve(overrideCount);
        for (size_t i = 0; i < overrideCount; ++i)
        {
            NagaOverrideView view{};
            naga_result_override(result.m_result, i, &view);

            WGPUShaderReflectionMeta::Override overrideInfo{};
            overrideInfo.identifier = CStringOrEmpty(view.identifier);
            overrideInfo.name = CStringOrEmpty(view.name);
            overrideInfo.type = CStringOrEmpty(view.type_name);
            overrideInfo.hasDefault = view.has_default;
            overrideInfo.staticallyUsed = view.statically_used;

            uint32_t supplied = 0;
            uint32_t accepted = 0;
            for (size_t ep = 0; ep < view.statically_used_by_count; ++ep)
            {
                if (!view.statically_used_by[ep])
                    continue;
                ++supplied;
                if (knownEps.count(view.statically_used_by[ep]))
                {
                    overrideInfo.staticallyUsedByEntryPoint.insert(view.statically_used_by[ep]);
                    ++accepted;
                }
            }
            overrideInfo.staticallyUsedByEntryPointAuthoritative =
                (supplied == 0) || (accepted == supplied);
            module->reflection.overrides.push_back(std::move(overrideInfo));
        }
#endif
    }

    void ConvertMessages(const CompileResult &result,
                         std::vector<WGPUCompilationMessageStorage> &messages)
    {
#if defined(PE_WEBGPU_WGSL) && PE_WEBGPU_WGSL
        if (!result.m_result)
        {
            return;
        }
        const size_t messageCount = naga_result_message_count(result.m_result);
        messages.reserve(messages.size() + messageCount);
        for (size_t i = 0; i < messageCount; ++i)
        {
            NagaMessageView view{};
            naga_result_message(result.m_result, i, &view);

            WGPUCompilationMessageStorage msg{};
            const std::string type = CStringOrEmpty(view.type);
            if (type == "warning")
                msg.type = WGPUCompilationMessageType_Warning;
            else if (type == "info")
                msg.type = WGPUCompilationMessageType_Info;
            else
                msg.type = WGPUCompilationMessageType_Error;
            msg.message = CStringOrEmpty(view.message);
            msg.lineNum = view.line_num;
            msg.linePos = view.line_pos;
            msg.offset = view.offset;
            msg.length = view.length;
            messages.push_back(std::move(msg));
        }
#endif
    }
} // namespace pwgpu::Wgsl
