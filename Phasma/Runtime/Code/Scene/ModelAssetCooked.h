#pragma once
#include <filesystem>
#include <vector>

namespace pe
{
    class ModelAsset;
    struct Skeleton;
    struct AnimationClip;

    // Cooked-mesh (".pemesh") pipeline.
    //
    // A ".pemesh" file stores a model's geometry in the *exact* byte layout the engine uploads to
    // the GPU: meshopt-optimized vertex/index/aabb streams plus the per-mesh, per-node, and material
    // tables. "Cooked" means there is no Assimp, meshoptimizer, tangent, or winding work at load
    // time; textures are loaded from relative paths that were copied next to the cooked file. Assimp
    // is editor-only and is used solely to import source models (glTF/FBX/OBJ) and cook them to this
    // format; the player (desktop and Android) only ever loads ".pemesh".
    //
    // The format is stream based so new vertex layouts (PBR gbuffer, shadow/depth, GUI, ...) are just
    // new stream ids; each stream is a raw, directly-uploadable byte blob with an explicit stride.
    class ModelAssetCooked
    {
    public:
        static constexpr const char *kExtension = ".pemesh";

        // Player/editor: load a cooked ".pemesh" into a fresh ModelAsset (Assimp-free, GPU ready).
        // Returns nullptr on a missing/invalid/incompatible file.
        static ModelAsset *Load(const std::filesystem::path &file) { return Load(file, false); }

        // The skeleton and clips of a cooked file without touching the GPU: the editor swaps clips into a live
        // model when the Animator re-saves it. Returns false on a missing/invalid/incompatible file.
        static bool ReadAnimations(const std::filesystem::path &file, Skeleton &skeleton, std::vector<AnimationClip> &clips);

        // Editor/cook side: serialize a ModelAsset's GPU-ready CPU data and referenced material
        // textures to a ".pemesh" file. The ModelAsset may come from any producer
        // (ModelAssetAssimp import, Primitives). Returns false on I/O failure. Skeleton and animation
        // clips are cooked too (skinned meshes); embedded textures (.glb) are extracted and written
        // next to the file as well (compressed PNG/JPG verbatim; raw embedded slots fall back to default).
        static bool WriteToFile(const ModelAsset *model, const std::filesystem::path &file);

        static bool IsCookedPath(const std::filesystem::path &file);

    private:
        // cpuOnly stops after the tables: streams, skeleton and clips parsed, no materials or buffers uploaded.
        static ModelAsset *Load(const std::filesystem::path &file, bool cpuOnly);
    };
} // namespace pe
