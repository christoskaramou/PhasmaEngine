#include "Scene/MaterialReflection.h"
#include "Scene/Backends/MaterialReflectionBackend.h"
#include "Scene/PassInfoAsset.h"

namespace pe
{
    MaterialLayout ReflectMaterialLayout(const PassInfoAsset &passInfo)
    {
        MaterialLayout layout;

        const PassVariant *surface = passInfo.GetVariant("surface");
        if (!surface || !surface->HasShaders())
            return layout;

        const ReflectedMaterialResources reflected = ReflectMaterialResourcesForBackend(*surface);
        if (reflected.rawMembers.empty())
            return layout;

        // Parse annotation
        MaterialAnnotation ann = ParseMaterialAnnotation(surface->materialAnnotation);

        // Build a lookup of raw struct members by name
        std::unordered_map<std::string, const StructMemberInfo *> memberByName;
        for (const auto &m : reflected.rawMembers)
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
                    field.baseType = raw.baseType;
                    field.vecSize = 1;
                    field.offset = raw.offset + fh.component * 4;
                    field.size = 4;

                    expanded = raw;
                    expanded.name = fh.fieldName;
                    expanded.vecSize = 1;
                    expanded.columns = 1;
                    expanded.offset = field.offset;
                    expanded.size = 4;
                }
                else if (fh.component == -2)
                {
                    // .xyz / .rgb — first 3 components of a vec4
                    field.baseType = raw.baseType;
                    field.vecSize = 3;
                    field.offset = raw.offset;
                    field.size = 12;

                    expanded = raw;
                    expanded.name = fh.fieldName;
                    expanded.vecSize = 3;
                    expanded.columns = 1;
                    expanded.offset = field.offset;
                    expanded.size = 12;
                }
                else
                {
                    // Whole member (e.g., baseColorFactor = baseColorFactor)
                    field.baseType = raw.baseType;
                    field.vecSize = raw.vecSize;
                    field.offset = raw.offset;
                    field.size = raw.size;

                    expanded = raw;
                    expanded.name = fh.fieldName;
                }

                layout.fields.push_back(field);
                layout.structMembers.push_back(expanded);
            }
            layout.totalByteSize = reflected.totalByteSize;
        }
        else
        {
            // No annotation: fall back to raw struct members
            layout.structMembers = reflected.rawMembers;
            for (const auto &member : reflected.rawMembers)
            {
                MaterialFieldDesc field;
                field.name = member.name;
                field.baseType = member.baseType;
                field.vecSize = member.vecSize;
                field.offset = member.offset;
                field.size = member.size;

                if (field.baseType == StructMemberBaseType::Float)
                {
                    if (field.vecSize == 4)
                        field.hint = MaterialWidgetHint::Color4;
                    else if (field.vecSize == 1)
                        field.hint = MaterialWidgetHint::Range01;
                }

                layout.fields.push_back(field);
            }
            layout.totalByteSize = reflected.totalByteSize;
        }

        layout.textureSlots = reflected.textureSlots;
        for (MaterialTextureSlot &slot : layout.textureSlots)
        {
            const std::string idxFieldName = slot.bindingName + "_idx";
            for (const auto &field : layout.fields)
            {
                if (field.name == idxFieldName && field.baseType == StructMemberBaseType::UInt)
                {
                    slot.byteOffset = field.offset;
                    break;
                }
            }
        }

        layout.valid = !layout.fields.empty() || !layout.textureSlots.empty();
        return layout;
    }
} // namespace pe
