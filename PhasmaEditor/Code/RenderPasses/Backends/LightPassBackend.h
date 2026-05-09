#pragma once

namespace pe
{
    namespace LightPassBackend
    {
        struct PostProcessFeatures
        {
            bool tonemapping = false;
            bool fsr2 = false;
        };

        PostProcessFeatures ResolvePostProcessFeatures(bool tonemappingRequested, bool fsr2Requested);
    } // namespace LightPassBackend
} // namespace pe
