#pragma once

#include <webgpu/webgpu.h>

namespace pe
{
    // One rectangle of one mip and layer, sampled into a rectangle of another (spec decision 5: BlitImage and the
    // GenerateMipMaps chain use a backend-owned WGSL blit). Coordinates are texels; a destination given high-to-low
    // flips the image on that axis.
    struct WebGpuBlitRegion
    {
        uint32_t srcMip = 0;
        uint32_t srcLayer = 0;
        float srcMin[2] = {};
        float srcMax[2] = {};
        uint32_t dstMip = 0;
        uint32_t dstLayer = 0;
        int32_t dstFrom[2] = {};
        int32_t dstTo[2] = {};
    };

    // Records a render pass that draws the source region into the destination region. `clear` discards the rest of
    // the destination mip (a whole-mip blit), otherwise it is kept. The destination needs RenderAttachment usage.
    void WebGpuBlitTexture(WGPUCommandEncoder encoder,
                           WGPUTexture src,
                           WGPUTexture dst,
                           const WebGpuBlitRegion &region,
                           bool linear,
                           bool clear);

    // Releases the cached pipelines and samplers (WebGpuRhiImpl::Shutdown).
    void WebGpuBlitShutdown();
} // namespace pe
