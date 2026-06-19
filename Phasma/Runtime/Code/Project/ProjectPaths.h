#pragma once


namespace pe
{
    [[nodiscard]] std::filesystem::path ResolveRuntimeStartupPath(const std::filesystem::path &path);
}
