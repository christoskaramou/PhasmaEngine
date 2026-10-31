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
} // namespace pe
