#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace pe
{
    class ModelAsset;

    bool PrepareGltfMaterialImport(const std::filesystem::path &file, std::string &bytes,
                                   std::vector<std::string> &materialNames, std::string &error);

    bool WriteGltfMaterialX(const ModelAsset &model, const std::vector<unsigned int> &materialIndices,
                            const std::filesystem::path &file,
                            std::vector<std::pair<std::string, std::string>> &references, std::string &error);
} // namespace pe
