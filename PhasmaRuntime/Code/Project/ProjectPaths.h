#pragma once

#include <filesystem>

namespace pe
{
    [[nodiscard]] std::filesystem::path ResolveRuntimeStartupPath(const std::filesystem::path &path);
}
