#pragma once

namespace pe
{
    class Image;

    struct PBRMaterial
    {
        vec4 baseColorFactor;
        float metallicFactor{};
        float roughnessFactor{};
        vec3 emissiveFactor;
        float alphaCutoff{};
        bool doubleSided{};
        RenderQueue renderQueue{};

        Image *baseColorTexture;
        Image *metallicRoughnessTexture;
        Image *normalTexture;
        Image *occlusionTexture;
        Image *emissiveTexture;
    };
}