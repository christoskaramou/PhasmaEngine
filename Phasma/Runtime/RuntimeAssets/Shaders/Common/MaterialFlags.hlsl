#ifndef MATERIAL_FLAGS_H_
#define MATERIAL_FLAGS_H_

static const uint TEX_BASE_COLOR_BIT  = 1u << 0;
static const uint TEX_METAL_ROUGH_BIT = 1u << 1;
static const uint TEX_NORMAL_BIT      = 1u << 2;
static const uint TEX_OCCLUSION_BIT   = 1u << 3;
static const uint TEX_EMISSIVE_BIT    = 1u << 4;

inline bool HasTexture(uint mask, uint bit)
{
    return (mask & bit) != 0;
}

#endif
