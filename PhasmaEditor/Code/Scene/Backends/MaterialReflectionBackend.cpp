#include "Scene/Backends/MaterialReflectionBackend.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "API/Vulkan/VulkanReflection.h"
#include "API/Vulkan/VulkanShaderImpl.h"
#include "Base/Path.h"
#include "spirv_cross.hpp"

namespace pe
{
    namespace
    {
        class ScopedShaderStages
        {
        public:
            ~ScopedShaderStages()
            {
                for (Shader *&shader : m_shaders)
                    Shader::Destroy(shader);
            }

            void Add(const std::string &sourcePath, const std::string &entryPoint, PeShaderStageFlags stage)
            {
                if (sourcePath.empty())
                    return;

                Shader *shader = Shader::Create({.sourcePath = Path::Assets + sourcePath,
                                                 .entryPoint = entryPoint,
                                                 .stage = stage,
                                                 .defines = std::vector<Define>{}});
                if (shader)
                    m_shaders.push_back(shader);
            }

            const std::vector<Shader *> &Get() const { return m_shaders; }

        private:
            std::vector<Shader *> m_shaders;
        };

        uint32_t Align4(uint32_t value)
        {
            return (value + 3u) & ~3u;
        }

        std::string GetSpirvResourceName(const spirv_cross::Compiler &compiler, spirv_cross::ID id)
        {
            std::string name = compiler.get_name(id);
            if (name.empty())
                name = compiler.get_fallback_name(id);
            return name;
        }

        bool ReflectMaterialBufferStructFromSpirv(Shader *shader,
                                                  const std::string &bufferName,
                                                  ReflectedMaterialResources &out)
        {
            const uint32_t *spirvData = GetVulkanShaderSpirv(shader);
            const size_t spirvWords = GetVulkanShaderSpirvSizeWords(shader);
            if (!spirvData || spirvWords == 0)
                return false;

            spirv_cross::Compiler compiler{spirvData, spirvWords};
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

            for (const spirv_cross::Resource &sbuf : resources.storage_buffers)
            {
                if (GetSpirvResourceName(compiler, sbuf.id) != bufferName)
                    continue;

                const spirv_cross::SPIRType &blockType = compiler.get_type(sbuf.base_type_id);
                const spirv_cross::SPIRType *structType = &blockType;

                if (blockType.member_types.size() == 1)
                {
                    const spirv_cross::SPIRType &memberType = compiler.get_type(blockType.member_types[0]);
                    if (!memberType.array.empty() && memberType.basetype == spirv_cross::SPIRType::Struct)
                        structType = &memberType;
                }

                out.rawMembers = ReflectStructMembersFromSpirv(compiler, *structType);
                out.totalByteSize = Align4(static_cast<uint32_t>(compiler.get_declared_struct_size(*structType)));
                return true;
            }

            return false;
        }

        void ReflectBindlessTextureSlotsFromSpirv(Shader *shader, std::vector<MaterialTextureSlot> &outSlots)
        {
            const uint32_t *spirvData = GetVulkanShaderSpirv(shader);
            const size_t spirvWords = GetVulkanShaderSpirvSizeWords(shader);
            if (!spirvData || spirvWords == 0)
                return;

            spirv_cross::Compiler compiler{spirvData, spirvWords};
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

            for (const spirv_cross::Resource &img : resources.separate_images)
            {
                const std::string name = GetSpirvResourceName(compiler, img.id);
                if (!name.starts_with("pe_tex_"))
                    continue;

                MaterialTextureSlot slot;
                slot.bindingName = name;
                slot.set = static_cast<int>(compiler.get_decoration(img.id, spv::DecorationDescriptorSet));
                slot.binding = static_cast<int>(compiler.get_decoration(img.id, spv::DecorationBinding));
                outSlots.push_back(slot);
            }
        }

        ReflectedMaterialResources ReflectMaterialResourcesFromVulkan(const PassVariant &variant)
        {
            ReflectedMaterialResources resources;

            ScopedShaderStages stages;
            stages.Add(variant.vertexShader, "mainVS", PE_SHADER_STAGE_VERTEX);
            stages.Add(variant.fragmentShader, "mainPS", PE_SHADER_STAGE_FRAGMENT);

            if (variant.materialBufferName.empty())
                return resources;

            for (Shader *shader : stages.Get())
            {
                if (ReflectMaterialBufferStructFromSpirv(shader, variant.materialBufferName, resources))
                    break;
            }

            if (resources.rawMembers.empty())
                return resources;

            for (Shader *shader : stages.Get())
                ReflectBindlessTextureSlotsFromSpirv(shader, resources.textureSlots);

            return resources;
        }
    } // namespace

    ReflectedMaterialResources ReflectMaterialResourcesForBackend(const PassVariant &variant)
    {
        if (RHII.GetApi() != PE_GRAPHICS_API_VULKAN || !variant.HasShaders())
            return {};

        return ReflectMaterialResourcesFromVulkan(variant);
    }
} // namespace pe
