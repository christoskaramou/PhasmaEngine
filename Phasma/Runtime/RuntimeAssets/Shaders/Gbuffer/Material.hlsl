#ifndef MATERIAL_H_
#define MATERIAL_H_

struct Material
{
    float3 albedo;
    float roughness;
    float3 F0;
    float metallic;
    float transmission;
};

#endif
