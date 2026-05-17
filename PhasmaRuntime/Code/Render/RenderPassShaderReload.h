#pragma once

#include <optional>

namespace pe
{
    class IRenderPassComponent;

    bool ReloadRenderPassShaders(IRenderPassComponent &renderPass, std::optional<size_t> changedShaderHash = std::nullopt);
} // namespace pe
