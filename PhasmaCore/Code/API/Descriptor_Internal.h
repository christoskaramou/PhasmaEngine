#pragma once

#include "API/Descriptor.h"

namespace pe
{
    struct DescriptorPool::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    struct DescriptorLayout::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    struct Descriptor::Impl : public NoCopy
    {
        virtual ~Impl() = default;
        virtual void Update(const std::vector<DescriptorBindingInfo> &bindingInfos,
                            const std::vector<DescriptorUpdateInfo> &updateInfos) = 0;
    };

    DescriptorPool::Impl *CreateDescriptorPoolImpl(DescriptorPool *owner, const DescriptorPoolDesc &desc);
    DescriptorLayout::Impl *CreateDescriptorLayoutImpl(DescriptorLayout *owner);
    Descriptor::Impl *CreateDescriptorImpl(Descriptor *owner);
} // namespace pe
