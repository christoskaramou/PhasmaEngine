#pragma once

namespace pe
{
    // One member of a shape group: a generated MaterialX module (PhasmaCook --mtlx, with PE_SHAPE regions) at its slot.
    struct OpenPBRShapeCode
    {
        uint32_t slot = 0;
        std::string hash;
        std::string code;
    };

    // The combined module text of one shape group; .testpool/tests/mtlx_combine.py is the reference and must match it
    // byte for byte. The default module's code outside its regions comes first, then code outside the regions that two
    // or more members share. Each member's remaining code goes into namespace pe_shape_<slot>, its statics renamed
    // pe_s<slot>_<name>. Dispatchers taking a leading `uint shape` switch on the slot. `slots` is the group's slot count:
    // the PE_SHAPES line marks slots without a member '-'. Throws on unbalanced code.
    std::string CombineOpenPBRShapes(const std::string &defaultCode, const std::vector<OpenPBRShapeCode> &members,
                                     uint32_t slots);

    // One MaterialX hit group of the RT pipelines: a shape group's combined hit shader (OpenPBRPassSources::hit).
    struct OpenPBRHitGroup
    {
        std::string key;    // OpenPBRPassSources::key: equal keys compiled the same text
        std::string source; // ShaderCache/MaterialX/group_<key>.hit.slang
        uint32_t members = 0;

        bool operator==(const OpenPBRHitGroup &) const = default;
    };

    // The MaterialX hit groups of an RT pipeline (RayTracingPass) and the TLAS (Scene::SetRTHitModules): hit group 0 is
    // the legacy one, hit group 1 + g compiles groups[g], and modules[i] runs in hit group 1 + moduleGroups[i] at its
    // shape slot (GetOpenPBRShapeSlot, through the pass's slot table).
    struct OpenPBRHitSet
    {
        std::vector<uint32_t> modules; // registry indices
        std::vector<uint32_t> moduleGroups;
        std::vector<OpenPBRHitGroup> groups;

        bool operator==(const OpenPBRHitSet &) const = default;

        // The SBT record offset an instance of the module uses: 1 + its group, or 0 (the legacy hit) outside the set.
        uint32_t HitGroup(uint32_t module) const
        {
            const auto it = std::find(modules.begin(), modules.end(), module);
            return it == modules.end() ? 0u : 1u + moduleGroups[it - modules.begin()];
        }
    };
} // namespace pe
