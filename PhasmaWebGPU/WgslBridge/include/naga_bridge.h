#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct NagaCompileResult NagaCompileResult;

    typedef struct NagaBindingView
    {
        uint32_t group;
        uint32_t binding;
        const char *name;
        const char *kind;
        const char *format;
        const char *access;
    } NagaBindingView;

    typedef struct NagaEntryPointView
    {
        const char *name;
        const char *stage;
        const NagaBindingView *statically_used;
        size_t statically_used_count;
    } NagaEntryPointView;

    typedef struct NagaOverrideView
    {
        const char *identifier;
        const char *name;
        const char *type_name;
        bool has_default;
        bool statically_used;
        const char *const *statically_used_by;
        size_t statically_used_by_count;
    } NagaOverrideView;

    typedef struct NagaMessageView
    {
        const char *type;
        const char *message;
        uint32_t line_num;
        uint32_t line_pos;
        uint32_t offset;
        uint32_t length;
    } NagaMessageView;

    typedef struct NagaConstantKv
    {
        const char *key;
        double value;
    } NagaConstantKv;

    void naga_install_panic_suppression(void);

    NagaCompileResult *naga_compile_wgsl(const char *source, size_t source_len);
    void naga_result_free(NagaCompileResult *result);

    const uint32_t *naga_result_spirv(const NagaCompileResult *result, size_t *out_word_count);

    size_t naga_result_entry_point_count(const NagaCompileResult *result);
    void naga_result_entry_point(const NagaCompileResult *result, size_t i, NagaEntryPointView *out);

    size_t naga_result_comparison_sampler_count(const NagaCompileResult *result);
    void naga_result_comparison_sampler(const NagaCompileResult *result, size_t i, NagaBindingView *out);

    size_t naga_result_override_count(const NagaCompileResult *result);
    void naga_result_override(const NagaCompileResult *result, size_t i, NagaOverrideView *out);

    size_t naga_result_message_count(const NagaCompileResult *result);
    void naga_result_message(const NagaCompileResult *result, size_t i, NagaMessageView *out);

    const uint32_t *naga_bake_with_constants(const char *source, size_t source_len,
                                             const NagaConstantKv *kvs, size_t kv_count,
                                             const char *entry_point,
                                             size_t *out_word_count);
    void naga_bake_free(const uint32_t *spirv);

    const char *naga_spirv_to_hlsl(const uint32_t *words, size_t word_count,
                                   const char *entry_point, const char *stage,
                                   size_t *out_len);
    void naga_string_free(const char *text);

#ifdef __cplusplus
}
#endif
