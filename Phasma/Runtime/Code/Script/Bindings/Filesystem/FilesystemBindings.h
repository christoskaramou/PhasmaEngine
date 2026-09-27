#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pe
{
    // Lua fs.read / fs.write and the native readFile / writeFile: paths resolve against Assets/ and may not leave
    // it; game-pack assets read from the pack and are never written. Writes create missing folders.
    [[nodiscard]] std::optional<std::string> ReadAssetsFile(const std::string &path);
    bool WriteAssetsFile(const std::string &path, std::string_view content, bool append);
} // namespace pe
