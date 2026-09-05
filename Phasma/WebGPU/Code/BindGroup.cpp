#include "BindGroup.h"
#include "DescriptorBuffer.h"
#include "Device.h"
#include "Utils.h"
#include "API/Image.h"

namespace
{
    void ReleaseDescriptorBufferSlice(WGPUBindGroupImpl *bg)
    {
        if (!bg || !bg->descriptorBufferValid)
            return;

        const uint64_t offset = bg->descriptorBufferOffset;
        const uint64_t size = bg->descriptorBufferSize;
        const uint64_t serial = bg->lastUsageSerial.load(std::memory_order_acquire);
        if (pwgpu::IsQueueSerialPending(bg->device, serial))
        {
            std::lock_guard<std::mutex> lock(bg->device->pendingResourceDeletionsMutex);
            bg->device->pendingDescriptorBufferSlices.push_back({offset, size, serial});
        }
        else
        {
            pwgpu::FreeDescriptorBufferSlice(bg->device, offset, size);
        }

        bg->descriptorBufferValid = false;
        bg->descriptorBufferOffset = 0;
        bg->descriptorBufferSize = 0;
    }
} // namespace

bool BglGroupEquivalent(const WGPUBindGroupLayoutImpl *a, const WGPUBindGroupLayoutImpl *b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    // §10.2.7 default pipeline layout: a BGL synthesized by an `auto`-layout
    // pipeline is identity-bound to that pipeline. Two such BGLs match iff
    // they come from the SAME pipeline (then we still require structural
    // equivalence below). A default BGL is NEVER equivalent to a BGL from a
    // different pipeline or to an explicit BGL.
    if (a->exclusivePipeline != b->exclusivePipeline)
        return false;
    if (a->entries.size() != b->entries.size())
        return false;

    auto kindOf = [](const WGPUBindGroupLayoutEntryResolved &e) -> int
    {
        if (e.buffer.type != WGPUBufferBindingType_BindingNotUsed)
            return 1;
        if (e.sampler.type != WGPUSamplerBindingType_BindingNotUsed)
            return 2;
        if (e.texture.sampleType != WGPUTextureSampleType_BindingNotUsed)
            return 3;
        if (e.storageTexture.access != WGPUStorageTextureAccess_BindingNotUsed)
            return 4;
        if (e.hasExternalTexture)
            return 5;
        return 0;
    };

    for (auto &ea : a->entries)
    {
        const WGPUBindGroupLayoutEntryResolved *eb = nullptr;
        for (auto &candidate : b->entries)
        {
            if (candidate.binding == ea.binding)
            {
                eb = &candidate;
                break;
            }
        }
        if (!eb)
            return false;
        if (ea.visibility != eb->visibility)
            return false;
        int ka = kindOf(ea), kb = kindOf(*eb);
        if (ka != kb)
            return false;
        switch (ka)
        {
        case 1:
            if (ea.buffer.type != eb->buffer.type ||
                ea.buffer.hasDynamicOffset != eb->buffer.hasDynamicOffset ||
                ea.buffer.minBindingSize != eb->buffer.minBindingSize)
                return false;
            break;
        case 2:
            if (ea.sampler.type != eb->sampler.type)
                return false;
            break;
        case 3:
            if (ea.texture.sampleType != eb->texture.sampleType ||
                ea.texture.viewDimension != eb->texture.viewDimension ||
                ea.texture.multisampled != eb->texture.multisampled)
                return false;
            break;
        case 4:
            if (ea.storageTexture.access != eb->storageTexture.access ||
                ea.storageTexture.format != eb->storageTexture.format ||
                ea.storageTexture.viewDimension != eb->storageTexture.viewDimension)
                return false;
            break;
        default:
            break;
        }
    }
    return true;
}

extern "C"
{

    void wgpuBindGroupLayoutAddRef(WGPUBindGroupLayout bgl)
    {
        if (bgl)
            bgl->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout bgl)
    {
        if (!bgl)
            return;
        if (bgl->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            pwgpu::DestroyDescriptorBufferLayout(bgl);
            if (bgl->layout)
                pe::DescriptorLayout::Destroy(bgl->layout);
            WGPUDeviceImpl *dev = bgl->device;
            delete bgl;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    void wgpuBindGroupLayoutSetLabel(WGPUBindGroupLayout bgl, WGPUStringView label)
    {
        if (bgl)
            bgl->label = pwgpu::ToString(label);
    }

    void wgpuBindGroupAddRef(WGPUBindGroup bg)
    {
        if (bg)
            bg->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuBindGroupRelease(WGPUBindGroup bg)
    {
        if (!bg)
            return;
        if (bg->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            ReleaseDescriptorBufferSlice(bg);
            if (bg->layout)
                wgpuBindGroupLayoutRelease(bg->layout);
            {
                std::vector<pe::Sampler *> samplers = std::move(bg->ownedSamplers);
                pe::Descriptor *descriptor = bg->descriptor;
                bg->descriptor = nullptr;
                pwgpu::DeferDestroy(bg->device, [samplers, descriptor]() mutable
                                    {
                                        for (pe::Sampler *sampler : samplers)
                                            pe::Sampler::Destroy(sampler);
                                        pe::Descriptor::Destroy(descriptor); });
            }
            for (auto &use : bg->bufferUses)
                wgpuBufferRelease(use.buffer);
            for (auto &use : bg->textureUses)
                wgpuTextureViewRelease(use.view);
            for (auto *sampler : bg->retainedSamplers)
                wgpuSamplerRelease(sampler);
            WGPUDeviceImpl *dev = bg->device;
            delete bg;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    void wgpuBindGroupSetLabel(WGPUBindGroup bg, WGPUStringView label)
    {
        if (bg)
            bg->label = pwgpu::ToString(label);
    }

} // extern "C"
