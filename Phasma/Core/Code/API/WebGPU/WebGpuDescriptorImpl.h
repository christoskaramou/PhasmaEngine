#pragma once

#include "API/Descriptor_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // WebGPU has no pools: bind groups are standalone objects.
    struct WebGpuDescriptorPoolImpl final : public DescriptorPool::Impl
    {
    };

    // One entry kind per engine binding, plus the WebGPU entries it becomes under the binding rule (decision 5):
    // engine binding b is WebGPU binding 2b, and a combined image sampler's sampler is 2b + 1.
    enum class WebGpuEntryKind : uint8_t
    {
        Uniform,
        Storage,         // read-write storage buffer
        ReadOnlyStorage, // StructuredBuffer / ByteAddressBuffer
        Texture,
        CombinedTextureSampler,
        Sampler,
        StorageTexture,
    };

    // Texture and sampler details come from the binding sidecar (decision 7). A binding the WGSL does not use has no
    // sidecar line; it gets a 2D float texture (depth under a depth read layout) and a matching sampler.
    struct WebGpuLayoutEntry
    {
        uint32_t binding = 0; // engine binding
        WebGpuEntryKind kind = WebGpuEntryKind::Uniform;
        bool dynamicOffset = false;
        WGPUTextureSampleType sampleType = WGPUTextureSampleType_Float;
        WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_2D;
        bool multisampled = false;
        WGPUSamplerBindingType samplerType = WGPUSamplerBindingType_Filtering;
        WGPUTextureFormat storageFormat = WGPUTextureFormat_Undefined;
        WGPUStorageTextureAccess storageAccess = WGPUStorageTextureAccess_WriteOnly;
    };

    struct WebGpuDescriptorLayoutImpl final : public DescriptorLayout::Impl
    {
        explicit WebGpuDescriptorLayoutImpl(DescriptorLayout *owner);
        ~WebGpuDescriptorLayoutImpl() override;

        static WebGpuDescriptorLayoutImpl *From(DescriptorLayout *layout)
        {
            return static_cast<WebGpuDescriptorLayoutImpl *>(layout->m_impl);
        }

        WGPUBindGroupLayout m_layout = nullptr;
        std::vector<WebGpuLayoutEntry> m_entries; // same order as the layout's binding infos
    };

    // Bind groups are immutable, so every Update builds a new one from the merged state of all bindings.
    struct WebGpuDescriptorImpl final : public Descriptor::Impl
    {
        explicit WebGpuDescriptorImpl(Descriptor *owner);
        ~WebGpuDescriptorImpl() override;

        static WebGpuDescriptorImpl *From(Descriptor *descriptor) { return static_cast<WebGpuDescriptorImpl *>(descriptor->m_impl); }

        void Update(const std::vector<DescriptorBindingInfo> &bindingInfos,
                    const std::vector<DescriptorUpdateInfo> &updateInfos) override;

        // Never null once constructed: unset slots hold dummy resources.
        WGPUBindGroup GetBindGroup() const { return m_group; }
        // Host-visible, non-staging buffers this group binds; a submit that binds the group uploads them (rule 2).
        const std::vector<Buffer *> &GetReferencedBuffers() const { return m_referenced; }

    private:
        void Rebuild();

        Descriptor *m_owner = nullptr;
        WGPUBindGroup m_group = nullptr;
        std::vector<DescriptorUpdateInfo> m_state; // merged per binding index
        std::vector<Buffer *> m_referenced;
    };
} // namespace pe
