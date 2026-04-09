#include "Scene/MaterialReflection.h"
#include "Scene/PassInfoAsset.h"
#include "API/Shader.h"
#include "API/Reflection.h"
#include "Base/Path.h"

namespace pe
{
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
        auto destroyStages = [&]()
        {
            for (auto &[shader, refl] : stages)
            {
                (void)refl;
                Shader::Destroy(shader);
            }
            stages.clear();
        };

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

        // Reflect the raw SPIR-V struct to get member offsets
        std::vector<StructMemberInfo> rawMembers;
        uint32_t rawTotalSize = 0;

        auto reflectRawStruct = [&](Shader *shader, const std::string &bufferName)
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

                rawMembers = Reflection::ReflectStructMembers(compiler, *structType);
                rawTotalSize = static_cast<uint32_t>(compiler.get_declared_struct_size(*structType));
                rawTotalSize = (rawTotalSize + 3u) & ~3u;
                return true;
            }
            return false;
        };

        if (!surface->materialBufferName.empty())
        {
            for (const auto &[shader, refl] : stages)
            {
                if (reflectRawStruct(shader, surface->materialBufferName))
                    break;
            }
        }

        if (rawMembers.empty())
        {
            destroyStages();
            return layout;
        }

        // Parse annotation
        MaterialAnnotation ann = ParseMaterialAnnotation(surface->materialAnnotation);

        // Build a lookup of raw struct members by name
        std::unordered_map<std::string, const StructMemberInfo *> memberByName;
        for (const auto &m : rawMembers)
            memberByName[m.name] = &m;

        if (!ann.fieldHints.empty())
        {
            // Annotation-driven: expand packed vec4s into individual named fields
            for (const auto &fh : ann.fieldHints)
            {
                // Determine which struct member this maps to
                const std::string &memberName = fh.structMember.empty() ? fh.fieldName : fh.structMember;
                auto it = memberByName.find(memberName);
                if (it == memberByName.end())
                    continue;

                const StructMemberInfo &raw = *it->second;

                MaterialFieldDesc field;
                field.name = fh.fieldName;
                field.hint = fh.widget;
                field.rangeMin = fh.rangeMin;
                field.rangeMax = fh.rangeMax;

                // Create expanded StructMemberInfo for BuildByteAddressData
                StructMemberInfo expanded;
                expanded.name = fh.fieldName;

                if (fh.component >= 0)
                {
                    // Single component of a vec (e.g., pbrParams.x)
                    field.baseType = raw.typeInfo.basetype;
                    field.vecSize = 1;
                    field.offset = raw.offset + fh.component * 4;
                    field.size = 4;

                    expanded.typeInfo = raw.typeInfo;
                    expanded.typeInfo.vecsize = 1;
                    expanded.typeInfo.columns = 1;
                    expanded.offset = field.offset;
                    expanded.size = 4;
                }
                else if (fh.component == -2)
                {
                    // .xyz / .rgb — first 3 components of a vec4
                    field.baseType = raw.typeInfo.basetype;
                    field.vecSize = 3;
                    field.offset = raw.offset;
                    field.size = 12;

                    expanded.typeInfo = raw.typeInfo;
                    expanded.typeInfo.vecsize = 3;
                    expanded.typeInfo.columns = 1;
                    expanded.offset = field.offset;
                    expanded.size = 12;
                }
                else
                {
                    // Whole member (e.g., baseColorFactor = baseColorFactor)
                    field.baseType = raw.typeInfo.basetype;
                    field.vecSize = raw.typeInfo.vecsize;
                    field.offset = raw.offset;
                    field.size = raw.size;

                    expanded = raw;
                    expanded.name = fh.fieldName;
                }

                layout.fields.push_back(field);
                layout.structMembers.push_back(expanded);
            }
            layout.totalByteSize = rawTotalSize;
        }
        else
        {
            // No annotation: fall back to raw struct members
            layout.structMembers = rawMembers;
            for (const auto &member : rawMembers)
            {
                MaterialFieldDesc field;
                field.name = member.name;
                field.baseType = member.typeInfo.basetype;
                field.vecSize = member.typeInfo.vecsize;
                field.offset = member.offset;
                field.size = member.size;

                if (field.baseType == spirv_cross::SPIRType::Float)
                {
                    if (field.vecSize == 4)
                        field.hint = MaterialWidgetHint::Color4;
                    else if (field.vecSize == 1)
                        field.hint = MaterialWidgetHint::Range01;
                }

                layout.fields.push_back(field);
            }
            layout.totalByteSize = rawTotalSize;
        }

        // Scan for pe_tex_ prefixed separate images (bindless texture slots)
        for (const auto &[shader, refl] : stages)
        {
            spirv_cross::Compiler compiler{shader->GetSpriv(), shader->Size()};
            auto resources = compiler.get_shader_resources();

            for (const auto &img : resources.separate_images)
            {
                std::string name = compiler.get_name(img.id);
                if (name.empty())
                    name = compiler.get_fallback_name(img.id);
                if (!name.starts_with("pe_tex_"))
                    continue;

                uint32_t descSet = compiler.get_decoration(img.id, spv::DecorationDescriptorSet);
                uint32_t descBinding = compiler.get_decoration(img.id, spv::DecorationBinding);

                MaterialTextureSlot slot;
                slot.bindingName = name;
                slot.set = static_cast<int>(descSet);
                slot.binding = static_cast<int>(descBinding);

                // Find matching uint index field in reflected struct (name + "_idx")
                std::string idxFieldName = name + "_idx";
                for (const auto &field : layout.fields)
                {
                    if (field.name == idxFieldName && field.baseType == spirv_cross::SPIRType::UInt)
                    {
                        slot.byteOffset = field.offset;
                        break;
                    }
                }

                layout.textureSlots.push_back(slot);
            }
        }

        layout.valid = !layout.fields.empty() || !layout.textureSlots.empty();
        destroyStages();
        return layout;
    }
} // namespace pe
