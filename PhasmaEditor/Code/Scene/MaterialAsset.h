#pragma once
#include <filesystem>

namespace pe
{
    struct Mesh;

    namespace MaterialAsset
    {
        // Serialize material parameters and texture paths to a .mat JSON file.
        void Save(const Mesh &mesh, const std::filesystem::path &path);

        // Deserialize a .mat JSON file into mesh material parameters and load textures.
        // Returns false if the file cannot be opened or parsed.
        bool Load(Mesh &mesh, const std::filesystem::path &path);
    }
} // namespace pe
