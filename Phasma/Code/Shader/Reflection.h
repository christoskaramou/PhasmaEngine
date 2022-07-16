#pragma once

namespace spirv_cross
{
    struct SPIRType;
}

namespace pe
{
    class ShaderInOutDesc
    {
    public:
        ShaderInOutDesc();

        uint32_t location = 0;
        std::shared_ptr<spirv_cross::SPIRType> type;
    };

    class CombinedImageSamplerDesc
    {
    public:
        CombinedImageSamplerDesc();

        uint32_t set = 0;
        uint32_t binding = 0;
    };

    class BufferDesc
    {
    public:
        BufferDesc();

        uint32_t set = 0;
        uint32_t binding = 0;
        std::shared_ptr<spirv_cross::SPIRType> type;
        size_t bufferSize = 0;
    };

    class PushConstantDesc
    {
    public:
        PushConstantDesc();

        size_t size = 0;
        uint32_t offset = 0;
        std::shared_ptr<spirv_cross::SPIRType> type;
    };

    class Shader;
    class VertexInputBindingDescription;
    class VertexInputAttributeDescription;

    class Reflection
    {
    public:
        void Init(Shader *shader);

        std::vector<VertexInputBindingDescription> GetVertexBindings();

        std::vector<VertexInputAttributeDescription> GetVertexAttributes();

        std::vector<ShaderInOutDesc> inputs{};
        std::vector<ShaderInOutDesc> outputs{};
        std::vector<CombinedImageSamplerDesc> samplers{};
        std::vector<BufferDesc> uniformBuffers{};
        std::vector<PushConstantDesc> pushConstantBuffers{};

    private:
        Shader *m_shader{};
    };

}