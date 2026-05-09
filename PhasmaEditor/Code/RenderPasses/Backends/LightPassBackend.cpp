#include "RenderPasses/Backends/LightPassBackend.h"
#include "API/RHI.h"

namespace pe
{
    namespace LightPassBackend
    {
        PostProcessFeatures ResolvePostProcessFeatures(bool tonemappingRequested, bool fsr2Requested)
        {
            const bool supportsInlinePostProcess = RHII.GetApi() != PE_GRAPHICS_API_DX12;

            return {
                .tonemapping = supportsInlinePostProcess && tonemappingRequested,
                .fsr2 = supportsInlinePostProcess && fsr2Requested,
            };
        }
    } // namespace LightPassBackend
} // namespace pe
