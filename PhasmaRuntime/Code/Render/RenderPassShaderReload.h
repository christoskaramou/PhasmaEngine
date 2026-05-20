#pragma once

#include "Base/Base.h"

#include <cstddef>
#include <optional>

namespace pe
{
    class IRenderPassComponent;

    bool ReloadRenderPassShaders(IRenderPassComponent &renderPass, std::optional<size_t> changedShaderHash = std::nullopt);
    void ReloadRenderPassShaders(const OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                 std::optional<size_t> changedShaderHash = std::nullopt);
} // namespace pe
