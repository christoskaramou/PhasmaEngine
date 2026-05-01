#pragma once
#include "spirv_cross/spirv_common.hpp"
#include "spirv_cross/spirv_cross.hpp"

namespace pe
{
    struct BaseDesc
    {
        std::string name;
        spirv_cross::SPIRType typeInfo = spirv_cross::SPIRType(spv::Op::OpNop);
    };

    struct StructMemberInfo
    {
        std::string name;
        spirv_cross::SPIRType typeInfo = spirv_cross::SPIRType(spv::Op::OpNop);
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    struct SpecializationConstantDesc : public BaseDesc
    {
        uint32_t constantId = 0;
    };

    struct ShaderInOutDesc : public BaseDesc
    {
        int binding = INT32_MIN;
        int location = INT32_MIN;
    };

    struct CombinedImageSamplerDesc : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
    };

    struct SamplerReflection : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
    };

    struct ImageReflection : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
    };

    struct BufferReflection : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
        size_t bufferSize = 0;
    };

    struct AccelerationStructureDesc : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
    };

    struct PushConstantDesc : public BaseDesc
    {
        bool operator==(const PushConstantDesc &other) const
        {
            return size == other.size;
        }

        std::string structName;
        size_t size = 0;
    };

    class Shader;
    class Descriptor;

    class Reflection
    {
    public:
        void Init(Shader *shader);
        std::vector<vk::VertexInputBindingDescription> GetVertexBindings();
        std::vector<vk::VertexInputAttributeDescription> GetVertexAttributes();
        std::vector<Descriptor *> GetDescriptors();
        const PushConstantDesc &GetPushConstantDesc() { return m_pushConstants; }

        const std::vector<BufferReflection> &GetStorageBuffers() const { return m_storageBuffers; }
        const std::vector<BufferReflection> &GetUniformBuffers() const { return m_uniformBuffers; }
        const std::vector<ImageReflection> &GetImages() const { return m_images; }
        const std::vector<ImageReflection> &GetStorageImages() const { return m_storageImages; }
        const std::vector<SamplerReflection> &GetSamplers() const { return m_samplers; }
        const std::vector<CombinedImageSamplerDesc> &GetCombinedImageSamplers() const { return m_combinedImageSamplers; }

        static std::vector<StructMemberInfo> ReflectStructMembers(
            const spirv_cross::Compiler &compiler,
            const spirv_cross::SPIRType &structType);

    private:
        std::vector<SpecializationConstantDesc> m_specializationConstants{};
        std::vector<ShaderInOutDesc> m_inputs{};
        std::vector<ShaderInOutDesc> m_outputs{};
        std::vector<CombinedImageSamplerDesc> m_combinedImageSamplers{};
        std::vector<SamplerReflection> m_samplers{};
        std::vector<ImageReflection> m_images{};
        std::vector<ImageReflection> m_storageImages{};
        std::vector<BufferReflection> m_uniformBuffers{};
        std::vector<BufferReflection> m_storageBuffers{};
        std::vector<AccelerationStructureDesc> m_accelerationStructures{};
        PushConstantDesc m_pushConstants{};
        Shader *m_shader{};
    };
} // namespace pe
