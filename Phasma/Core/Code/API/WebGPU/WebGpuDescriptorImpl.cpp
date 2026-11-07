#include "API/WebGPU/WebGpuDescriptorImpl.h"
#include "API/Buffer.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/Sampler.h"
#include "API/WebGPU/WebGpuBufferImpl.h"
#include "API/WebGPU/WebGpuFormat.h"
#include "API/WebGPU/WebGpuImageImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "API/WebGPU/WebGpuSamplerImpl.h"

namespace pe
{
    namespace
    {
        constexpr WGPUShaderStage kAllStages = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        // Read-write storage is not allowed in vertex shaders.
        constexpr WGPUShaderStage kWritableStages = WGPUShaderStage_Fragment | WGPUShaderStage_Compute;

        bool IsDepthReadLayout(PeImageLayout layout)
        {
            return layout == PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
                   layout == PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
                   layout == PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        }

        // The binding sidecar's facts (decision 7), or the defaults of a binding the WGSL does not use.
        void ApplyFacts(WebGpuLayoutEntry &entry, const DescriptorBindingInfo &info, const std::string &layoutName)
        {
            const WebGpuBindingFacts &f = info.webgpu;

            // The converter narrows storage the shader never writes to read-only, and the layout must match it.
            if (entry.kind == WebGpuEntryKind::Storage && f.buffer == WGPUBufferBindingType_ReadOnlyStorage)
                entry.kind = WebGpuEntryKind::ReadOnlyStorage;

            const bool depthLayout = IsDepthReadLayout(info.imageLayout);
            entry.sampleType = depthLayout ? WGPUTextureSampleType_Depth : WGPUTextureSampleType_Float;
            if (f.sampleType)
                entry.sampleType = static_cast<WGPUTextureSampleType>(f.sampleType);

            if (f.viewDimension)
                entry.viewDimension = static_cast<WGPUTextureViewDimension>(f.viewDimension);

            entry.multisampled = f.multisampled;

            // Only a float texture may sit under a filtering sampler.
            const bool filterable = entry.sampleType == WGPUTextureSampleType_Float;
            entry.samplerType = filterable ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
            if (f.sampler)
                entry.samplerType = static_cast<WGPUSamplerBindingType>(f.sampler);

            if (entry.kind != WebGpuEntryKind::StorageTexture)
                return;

            PE_ERROR_IF(!f.storageFormat,
                        "WebGPU layout '%s': storage image binding %u ('%s') has no format in the binding sidecar",
                        layoutName.c_str(),
                        info.binding,
                        info.name.c_str());
            entry.storageFormat = static_cast<WGPUTextureFormat>(f.storageFormat);
            entry.storageAccess = static_cast<WGPUStorageTextureAccess>(f.storageAccess);
        }

        WebGpuLayoutEntry ToEntry(const DescriptorBindingInfo &info, const std::string &layoutName)
        {
            // Bindless is off (maxBindlessTextures 0; decision 9 replaces the scene texture table with per-material
            // bind groups), and resource arrays do not survive the web profile (decision 10).
            PE_ERROR_IF(info.bindless,
                        "WebGPU layout '%s': binding %u ('%s') is bindless, which WebGPU lacks (spec decision 9)",
                        layoutName.c_str(),
                        info.binding,
                        info.name.c_str());
            PE_ERROR_IF(info.count != 1,
                        "WebGPU layout '%s': binding %u is an array of %u, which WebGPU bind groups cannot hold",
                        layoutName.c_str(),
                        info.binding,
                        info.count);

            WebGpuLayoutEntry entry{};
            entry.binding = info.binding;
            switch (info.type)
            {
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                entry.kind = WebGpuEntryKind::Uniform;
                break;
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                entry.kind = WebGpuEntryKind::Uniform;
                entry.dynamicOffset = true;
                break;
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                entry.kind = WebGpuEntryKind::Storage;
                break;
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                entry.kind = WebGpuEntryKind::Storage;
                entry.dynamicOffset = true;
                break;
            case PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER:
            case PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER:
                entry.kind = WebGpuEntryKind::ReadOnlyStorage;
                break;
            case PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                entry.kind = WebGpuEntryKind::Texture;
                break;
            case PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                entry.kind = WebGpuEntryKind::CombinedTextureSampler;
                break;
            case PE_DESCRIPTOR_TYPE_SAMPLER:
                entry.kind = WebGpuEntryKind::Sampler;
                break;
            case PE_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                entry.kind = WebGpuEntryKind::StorageTexture;
                break;
            default:
                PE_ERROR("WebGPU layout '%s': binding %u has a type WebGPU lacks (%u)",
                         layoutName.c_str(),
                         info.binding,
                         static_cast<uint32_t>(info.type));
                break;
            }

            ApplyFacts(entry, info, layoutName);
            return entry;
        }

        void AppendLayoutEntries(const WebGpuLayoutEntry &entry, std::vector<WGPUBindGroupLayoutEntry> &out)
        {
            WGPUBindGroupLayoutEntry e{};
            e.binding = entry.binding * 2;
            e.visibility = kAllStages;
            switch (entry.kind)
            {
            case WebGpuEntryKind::Uniform:
                e.buffer.type = WGPUBufferBindingType_Uniform;
                e.buffer.hasDynamicOffset = entry.dynamicOffset;
                break;
            case WebGpuEntryKind::Storage:
                e.visibility = kWritableStages;
                e.buffer.type = WGPUBufferBindingType_Storage;
                e.buffer.hasDynamicOffset = entry.dynamicOffset;
                break;
            case WebGpuEntryKind::ReadOnlyStorage:
                e.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                e.buffer.hasDynamicOffset = entry.dynamicOffset;
                break;
            case WebGpuEntryKind::Texture:
            case WebGpuEntryKind::CombinedTextureSampler:
                e.texture.sampleType = entry.sampleType;
                e.texture.viewDimension = entry.viewDimension;
                e.texture.multisampled = entry.multisampled;
                break;
            case WebGpuEntryKind::Sampler:
                e.sampler.type = entry.samplerType;
                break;
            case WebGpuEntryKind::StorageTexture:
                if (entry.storageAccess != WGPUStorageTextureAccess_ReadOnly)
                    e.visibility = kWritableStages;

                e.storageTexture.access = entry.storageAccess;
                e.storageTexture.format = entry.storageFormat;
                e.storageTexture.viewDimension = entry.viewDimension;
                break;
            }

            out.push_back(e);
            if (entry.kind != WebGpuEntryKind::CombinedTextureSampler)
                return;

            WGPUBindGroupLayoutEntry sampler{};
            sampler.binding = entry.binding * 2 + 1;
            sampler.visibility = kAllStages;
            sampler.sampler.type = entry.samplerType;
            out.push_back(sampler);
        }

        // An engine sampler of the entry's type, else a dummy. A non-filtering entry always takes the dummy: engine
        // samplers may filter, and the WebGPU sampler object does not say.
        WGPUSampler PickSampler(const WebGpuLayoutEntry &entry, Sampler *sampler, const std::string &descriptorName)
        {
            if (!sampler || entry.samplerType == WGPUSamplerBindingType_NonFiltering)
                return WebGpuRhiImpl::Get()->DummySampler(entry.samplerType);

            const WebGpuSamplerImpl *impl = WebGpuSamplerImpl::From(sampler);
            const bool comparisonEntry = entry.samplerType == WGPUSamplerBindingType_Comparison;
            PE_ERROR_IF(impl->m_comparison != comparisonEntry,
                        "WebGPU descriptor '%s': %s sampler '%s' bound to %s sampler binding %u",
                        descriptorName.c_str(),
                        impl->m_comparison ? "comparison" : "non-comparison",
                        sampler->GetName().c_str(),
                        comparisonEntry ? "a comparison" : "a filtering",
                        entry.binding);
            return impl->m_sampler;
        }

        Buffer *FirstOf(const std::vector<Buffer *> &buffers)
        {
            return buffers.empty() ? nullptr : buffers[0];
        }
    } // namespace

    WebGpuDescriptorLayoutImpl::WebGpuDescriptorLayoutImpl(DescriptorLayout *owner)
    {
        std::vector<WGPUBindGroupLayoutEntry> entries;
        for (const DescriptorBindingInfo &info : owner->GetBindingInfos())
        {
            m_entries.push_back(ToEntry(info, owner->GetName()));
            AppendLayoutEntries(m_entries.back(), entries);
        }

        WGPUBindGroupLayoutDescriptor desc{};
        desc.label = {owner->GetName().c_str(), owner->GetName().size()};
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        m_layout = wgpuDeviceCreateBindGroupLayout(WebGpuRhiImpl::Get()->m_device, &desc);
        PE_ERROR_IF(!m_layout, "WebGPU layout '%s': creation failed", owner->GetName().c_str());
    }

    WebGpuDescriptorLayoutImpl::~WebGpuDescriptorLayoutImpl()
    {
        if (m_layout)
            wgpuBindGroupLayoutRelease(m_layout);
    }

    WebGpuDescriptorImpl::WebGpuDescriptorImpl(Descriptor *owner)
        : m_owner{owner},
          m_state(owner->GetBindingInfos().size())
    {
        Rebuild(); // every slot starts on a dummy, so the group is valid before the first Update
    }

    WebGpuDescriptorImpl::~WebGpuDescriptorImpl()
    {
        if (m_group)
            wgpuBindGroupRelease(m_group);
    }

    void WebGpuDescriptorImpl::Update(const std::vector<DescriptorBindingInfo> &,
                                      const std::vector<DescriptorUpdateInfo> &updateInfos)
    {
        // Updates hold only what was set since the last Update; keep the rest.
        for (size_t i = 0; i < updateInfos.size() && i < m_state.size(); ++i)
        {
            const DescriptorUpdateInfo &update = updateInfos[i];
            DescriptorUpdateInfo &state = m_state[i];
            if (!update.buffers.empty())
            {
                state.buffers = update.buffers;
                state.offsets = update.offsets;
                state.ranges = update.ranges;
            }

            if (!update.views.empty())
                state.views = update.views;

            if (!update.samplers.empty())
                state.samplers = update.samplers;
        }

        Rebuild();
    }

    void WebGpuDescriptorImpl::Rebuild()
    {
        WebGpuRhiImpl *web = WebGpuRhiImpl::Get();
        const WebGpuDescriptorLayoutImpl *layout = WebGpuDescriptorLayoutImpl::From(m_owner->GetLayout());

        std::vector<WGPUBindGroupEntry> entries;
        m_referenced.clear();
        for (size_t i = 0; i < layout->m_entries.size(); ++i)
        {
            const WebGpuLayoutEntry &entry = layout->m_entries[i];
            const DescriptorUpdateInfo &state = m_state[i];
            WGPUBindGroupEntry e{};
            e.binding = entry.binding * 2;

            const bool isBuffer = entry.kind == WebGpuEntryKind::Uniform ||
                                  entry.kind == WebGpuEntryKind::Storage ||
                                  entry.kind == WebGpuEntryKind::ReadOnlyStorage;
            if (isBuffer)
            {
                Buffer *buffer = FirstOf(state.buffers);
                e.buffer = web->DummyBuffer();
                e.size = WGPU_WHOLE_SIZE;
                if (buffer)
                {
                    WebGpuBufferImpl *impl = WebGpuBufferImpl::From(buffer);
                    // Rule 3: a GPU write would be overwritten by the buffer's shadow at its next upload.
                    PE_ERROR_IF(entry.kind == WebGpuEntryKind::Storage && impl->IsHostVisible(),
                                "WebGPU descriptor '%s': host-visible buffer '%s' bound to read-write storage binding %u",
                                m_owner->m_name.c_str(),
                                buffer->GetName().c_str(),
                                entry.binding);

                    const uint64_t offset = state.offsets.empty() ? 0 : state.offsets[0];
                    const uint64_t range = state.ranges.empty() ? 0 : state.ranges[0];
                    e.buffer = impl->m_buffer;
                    e.offset = offset;
                    e.size = range ? range : WGPU_WHOLE_SIZE;
                    if (impl->UploadsWhenReferenced())
                        m_referenced.push_back(buffer);
                }

                entries.push_back(e);
                continue;
            }

            Sampler *sampler = state.samplers.empty() ? nullptr : state.samplers[0];
            if (entry.kind == WebGpuEntryKind::Sampler)
            {
                e.sampler = PickSampler(entry, sampler, m_owner->m_name);
                entries.push_back(e);
                continue;
            }

            ImageView *view = state.views.empty() ? nullptr : state.views[0];
            if (entry.kind == WebGpuEntryKind::StorageTexture)
            {
                e.textureView = web->DummyStorageView(entry.viewDimension, entry.storageFormat);
                if (view)
                    e.textureView = WebGpuImageViewImpl::From(view)->m_view;

                entries.push_back(e);
                continue;
            }

            e.textureView = web->DummyView(entry.viewDimension, entry.sampleType, entry.multisampled);
            if (view)
            {
                // A depth view fits depth and unfilterable-float entries only (WebGPU's texture format table).
                const bool depthView = IsDepthOrStencil(WebGpuImageImpl::From(view->GetParent())->m_format);
                PE_ERROR_IF(depthView && entry.sampleType == WGPUTextureSampleType_Float,
                            "WebGPU descriptor '%s': depth view '%s' bound to float texture binding %u",
                            m_owner->m_name.c_str(),
                            view->GetName().c_str(),
                            entry.binding);
                e.textureView = WebGpuImageViewImpl::From(view)->m_view;
            }

            entries.push_back(e);
            if (entry.kind != WebGpuEntryKind::CombinedTextureSampler)
                continue;

            // A combined binding without a sampler takes the image's own, as the other backends do.
            if (!sampler && view)
                sampler = view->GetParent()->GetSampler();

            WGPUBindGroupEntry s{};
            s.binding = entry.binding * 2 + 1;
            s.sampler = PickSampler(entry, sampler, m_owner->m_name);
            entries.push_back(s);
        }

        WGPUBindGroupDescriptor desc{};
        desc.label = {m_owner->m_name.c_str(), m_owner->m_name.size()};
        desc.layout = layout->m_layout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(web->m_device, &desc);
        PE_ERROR_IF(!group, "WebGPU descriptor '%s': bind group creation failed", m_owner->m_name.c_str());

        // The previous group may still be in a recorded command buffer; WebGPU keeps it alive until that finishes.
        if (m_group)
            wgpuBindGroupRelease(m_group);

        m_group = group;
    }
} // namespace pe
