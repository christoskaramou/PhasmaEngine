#pragma once

namespace pe
{
    struct Mesh;
    class Material;

    namespace MaterialIO
    {
        // --- Legacy API (operates on Mesh directly) ---

        // Serialize material parameters and texture paths to a .mat JSON file.
        void Save(const Mesh &mesh, const std::filesystem::path &path);

        // Deserialize a .mat JSON file into mesh material parameters and load textures.
        // Returns false if the file cannot be opened or parsed.
        bool Load(Mesh &mesh, const std::filesystem::path &path);

        // --- Material API ---

        // Save a Material to a versioned .mat JSON file.
        void SaveMaterial(const Material &material, const std::filesystem::path &path);

        // Load a .mat file into a Material. Handles both legacy (v1) and new (v2) formats.
        // Returns false if the file cannot be opened or parsed.
        bool LoadMaterial(Material &material, const std::filesystem::path &path);
    } // namespace MaterialIO
} // namespace pe
