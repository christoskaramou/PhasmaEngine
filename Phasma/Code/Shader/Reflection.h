#pragma once
#include "spirv_cross/spirv_cross.hpp"
#include "Renderer/Vertex.h"

namespace pe
{
    struct BaseDesc
    {
        std::string name;
        spirv_cross::SPIRType typeInfo = spirv_cross::SPIRType(spv::Op::OpNop);
    };

    struct SpecializationConstantDesc : public BaseDesc
    {
        uint32_t constantId = 0;
    };

    struct ShaderInOutDesc : public BaseDesc
    {
        int location = INT32_MIN;
    };

    struct CombinedImageSamplerDesc : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
    };

    struct SamplerDesc : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
    };

    struct ImageDesc : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
    };

    struct BufferDesc : public BaseDesc
    {
        int set = INT32_MIN;
        int binding = INT32_MIN;
        size_t bufferSize = 0;
    };

    struct PushConstantDesc : public BaseDesc
    {
        bool operator==(const PushConstantDesc &other) const
        {
            return size == other.size && structName == other.structName;
        }

        std::string structName;
        size_t size = 0;
    };

    class Shader;
    struct VertexInputBindingDescription;
    struct VertexInputAttributeDescription;
    class Descriptor;

    class Reflection
    {
    public:
        ~Reflection();

        void Init(Shader *shader);

        std::vector<VertexInputBindingDescription> GetVertexBindings();

        std::vector<VertexInputAttributeDescription> GetVertexAttributes();

        std::vector<Descriptor *> GetDescriptors();

        const PushConstantDesc &GetPushConstantDesc() { return m_pushConstants; }

    private:
        std::vector<SpecializationConstantDesc> m_specializationConstants{};
        std::vector<ShaderInOutDesc> m_inputs{};
        std::vector<ShaderInOutDesc> m_outputs{};
        std::vector<CombinedImageSamplerDesc> m_combinedImageSamplers{};
        std::vector<SamplerDesc> m_samplers{};
        std::vector<ImageDesc> m_images{};
        std::vector<ImageDesc> m_storageImages{};
        std::vector<BufferDesc> m_uniformBuffers{};
        std::vector<BufferDesc> m_storageBuffers{};
        PushConstantDesc m_pushConstants{};
        Shader *m_shader{};
    };
}