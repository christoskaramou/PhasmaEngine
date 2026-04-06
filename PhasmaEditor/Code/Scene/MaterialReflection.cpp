#include "Scene/MaterialReflection.h"
#include "Scene/PassInfoAsset.h"
#include "API/Shader.h"
#include "API/Reflection.h"
#include "Base/Path.h"

namespace pe
{
    static MaterialWidgetHint InferWidgetFromType(spirv_cross::SPIRType::BaseType baseType, uint32_t vecSize)
    {
        if (baseType == spirv_cross::SPIRType::Float)
        {
            if (vecSize == 4)
                return MaterialWidgetHint::Color4;
            if (vecSize == 1)
                return MaterialWidgetHint::Range01;
        }
        return MaterialWidgetHint::Auto;
    }

    MaterialLayout ReflectMaterialLayout(const PassInfoAsset &passInfo)
    {
        MaterialLayout layout;

        const PassVariant *surface = passInfo.GetVariant("surface");
        if (!surface || !surface->HasShaders())
            return layout;

        struct ShaderRefl
        {
            Shader *shader;
            const Reflection *refl;
        };
        std::vector<ShaderRefl> stages;

        if (!surface->vertexShader.empty())
        {
            Shader *vs = Shader::Create(
                Path::Assets + surface->vertexShader,
                vk::ShaderStageFlagBits::eVertex,
                "mainVS",
                std::vector<Define>{},
                ShaderCodeType::HLSL);
            if (vs)
                stages.push_back({vs, &vs->GetReflection()});
        }
        if (!surface->fragmentShader.empty())
        {
            Shader *fs = Shader::Create(
                Path::Assets + surface->fragmentShader,
                vk::ShaderStageFlagBits::eFragment,
                "mainPS",
                std::vector<Define>{},
                ShaderCodeType::HLSL);
            if (fs)
                stages.push_back({fs, &fs->GetReflection()});
        }

        auto reflectBufferStruct = [&](Shader *shader, const std::string &bufferName, const MaterialAnnotation &ann)
        {
            spirv_cross::Compiler compiler{shader->GetSpriv(), shader->Size()};
            auto resources = compiler.get_shader_resources();

            for (const auto &sbuf : resources.storage_buffers)
            {
                std::string name = compiler.get_name(sbuf.id);
                if (name.empty())
                    name = compiler.get_fallback_name(sbuf.id);
                if (name != bufferName)
                    continue;

                const auto &blockType = compiler.get_type(sbuf.base_type_id);
                const spirv_cross::SPIRType *structType = &blockType;

                if (blockType.member_types.size() == 1)
                {
                    const auto &memberType = compiler.get_type(blockType.member_types[0]);
                    if (!memberType.array.empty() && memberType.basetype == spirv_cross::SPIRType::Struct)
                        structType = &memberType;
                }

                layout.structMembers = Reflection::ReflectStructMembers(compiler, *structType);
                for (const auto &member : layout.structMembers)
                {
                    MaterialFieldDesc field;
                    field.name = member.name;
                    field.baseType = member.typeInfo.basetype;
                    field.vecSize = member.typeInfo.vecsize;
                    field.offset = member.offset;
                    field.size = member.size;

                    field.hint = InferWidgetFromType(field.baseType, field.vecSize);
                    for (const auto &fh : ann.fieldHints)
                    {
                        if (fh.fieldName == field.name)
                        {
                            field.hint = fh.widget;
                            field.rangeMin = fh.rangeMin;
                            field.rangeMax = fh.rangeMax;
                            break;
                        }
                    }

                    layout.fields.push_back(field);
                }

                layout.totalByteSize = static_cast<uint32_t>(compiler.get_declared_struct_size(*structType));
                layout.totalByteSize = (layout.totalByteSize + 3u) & ~3u;
                return true;
            }
            return false;
        };

        if (!surface->materialBufferName.empty())
        {
            MaterialAnnotation ann = ParseMaterialAnnotation(surface->materialAnnotation);
            for (const auto &[shader, refl] : stages)
            {
                if (reflectBufferStruct(shader, surface->materialBufferName, ann))
                    break;
            }
        }

        layout.valid = !layout.fields.empty() || !layout.textureSlots.empty();
        return layout;
    }
} // namespace pe
