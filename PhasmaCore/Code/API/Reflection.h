#pragma once

#include "API/RHITypes.h"
#include "Base/Enums.h"

namespace pe
{
    class Shader;
    class Descriptor;

    enum class StructMemberBaseType : uint32_t
    {
        Unknown = 0,
        Boolean,
        SByte,
        UByte,
        Short,
        UShort,
        Int,
        UInt,
        Int64,
        UInt64,
        Half,
        Float,
        Double,
        Struct,
    };

    struct StructMemberInfo
    {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
        StructMemberBaseType baseType = StructMemberBaseType::Unknown;
        uint32_t vecSize = 1;
        uint32_t columns = 1;
    };

    struct SpecializationConstantDesc
    {
        std::string name;
        uint32_t constantId = 0;
        StructMemberBaseType baseType = StructMemberBaseType::Unknown;
        uint32_t vecSize = 1;
        uint32_t columns = 1;
    };

    struct ShaderInOutDesc
    {
        std::string name;
        int location = INT32_MIN;
        int binding = INT32_MIN;
        uint32_t semanticIndex = 0;
        ::PeFormat format = ::PE_FORMAT_UNDEFINED;
        uint32_t size = 0;
    };

    struct VertexInputBinding
    {
        uint32_t binding = 0;
        uint32_t stride = 0;
    };

    struct VertexInputAttribute
    {
        uint32_t location = 0;
        uint32_t binding = 0;
        ::PeFormat format = ::PE_FORMAT_UNDEFINED;
        uint32_t offset = 0;
    };

    // Disambiguates buffer-shaped resources. RW (UAV on DX12) is the default; the read-only
    // variants matter only on DX12 — Vulkan collapses both to eStorageBuffer at the descriptor
    // layer. Splitting at the reflection layer is necessary because emitting a UAV view for a
    // read-only StructuredBuffer/ByteAddressBuffer on DX12 is a validation failure.
    enum class PeBufferKind : uint32_t
    {
        StorageRW = 0, // RWStructuredBuffer / RWByteAddressBuffer / Append/Consume / RWBuffer
        Structured,    // read-only StructuredBuffer<T>
        ByteAddress,   // read-only ByteAddressBuffer
    };

    struct BufferReflection
    {
        std::string name;
        int set = INT32_MIN;
        int binding = INT32_MIN;
        int dxRegister = INT32_MIN;
        int dxSpace = INT32_MIN;
        uint32_t count = 1;
        size_t bufferSize = 0;
        PeBufferKind kind = PeBufferKind::StorageRW;
    };

    struct ImageReflection
    {
        std::string name;
        int set = INT32_MIN;
        int binding = INT32_MIN;
        int dxRegister = INT32_MIN;
        int dxSpace = INT32_MIN;
        uint32_t count = 1;
    };

    struct SamplerReflection
    {
        std::string name;
        int set = INT32_MIN;
        int binding = INT32_MIN;
        int dxRegister = INT32_MIN;
        int dxSpace = INT32_MIN;
        uint32_t count = 1;
    };

    struct CombinedImageSamplerReflection
    {
        std::string name;
        int set = INT32_MIN;
        int binding = INT32_MIN;
        int dxRegister = INT32_MIN;
        int dxSpace = INT32_MIN;
        uint32_t count = 1;
    };

    struct AccelerationStructureReflection
    {
        std::string name;
        int set = INT32_MIN;
        int binding = INT32_MIN;
        int dxRegister = INT32_MIN;
        int dxSpace = INT32_MIN;
        uint32_t count = 1;
    };

    struct PushConstantDesc
    {
        std::string name;
        std::string structName;
        size_t size = 0;

        bool operator==(const PushConstantDesc &other) const { return size == other.size; }
    };

    class Reflection
    {
    public:
        void Init(Shader *shader);
        std::vector<VertexInputBinding> GetVertexBindings() const;
        std::vector<VertexInputAttribute> GetVertexAttributes() const;
        std::vector<Descriptor *> GetDescriptors() const;
        const PushConstantDesc &GetPushConstantDesc() const { return m_pushConstants; }

        const std::vector<BufferReflection> &GetStorageBuffers() const { return m_storageBuffers; }
        const std::vector<BufferReflection> &GetUniformBuffers() const { return m_uniformBuffers; }
        const std::vector<ImageReflection> &GetImages() const { return m_images; }
        const std::vector<ShaderInOutDesc> &GetInputs() const { return m_inputs; }
        const std::vector<ImageReflection> &GetStorageImages() const { return m_storageImages; }
        const std::vector<SamplerReflection> &GetSamplers() const { return m_samplers; }
        const std::vector<CombinedImageSamplerReflection> &GetCombinedImageSamplers() const { return m_combinedImageSamplers; }
        const std::vector<AccelerationStructureReflection> &GetAccelerationStructures() const { return m_accelerationStructures; }

        Shader *GetShader() const { return m_shader; }

    private:
        friend void PopulateReflectionFromSpirv(Reflection &refl, Shader *shader);
#if defined(PE_WIN32)
        friend void PopulateReflectionFromDxil(Reflection &refl, Shader *shader);
#endif

        std::vector<SpecializationConstantDesc> m_specializationConstants{};
        std::vector<ShaderInOutDesc> m_inputs{};
        std::vector<ShaderInOutDesc> m_outputs{};
        std::vector<CombinedImageSamplerReflection> m_combinedImageSamplers{};
        std::vector<SamplerReflection> m_samplers{};
        std::vector<ImageReflection> m_images{};
        std::vector<ImageReflection> m_storageImages{};
        std::vector<BufferReflection> m_uniformBuffers{};
        std::vector<BufferReflection> m_storageBuffers{};
        std::vector<AccelerationStructureReflection> m_accelerationStructures{};
        PushConstantDesc m_pushConstants{};
        Shader *m_shader{};
    };
} // namespace pe
