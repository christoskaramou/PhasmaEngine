#pragma once
#include <filesystem>

namespace pe
{
    class ModelAsset;

    // Cooked-mesh (".pemesh") pipeline.
    //
    // A ".pemesh" file stores a model's geometry in the *exact* byte layout the engine uploads to
    // the GPU: meshopt-optimized vertex/index/aabb streams plus the per-mesh and per-node tables.
    // "Cooked" means there is no work to do at load time beyond a memcpy into the scene's combined
    // buffers — no Assimp, no meshoptimizer, no tangent/winding fixups. Assimp is editor-only and is
    // used solely to import source models (glTF/FBX/OBJ) and cook them to this format; the player
    // (desktop and Android) only ever loads ".pemesh".
    //
    // The format is stream based so new vertex layouts (PBR gbuffer, shadow/depth, GUI, ...) are just
    // new stream ids; each stream is a raw, directly-uploadable byte blob with an explicit stride.
    class ModelAssetCooked
    {
    public:
        static constexpr const char *kExtension = ".pemesh";

        // Player/editor: load a cooked ".pemesh" into a fresh ModelAsset (Assimp-free, GPU ready).
        // Returns nullptr on a missing/invalid/incompatible file.
        static ModelAsset *Load(const std::filesystem::path &file);

        // Editor/cook side: serialize a ModelAsset's GPU-ready CPU data to a ".pemesh" file. The
        // ModelAsset may come from any producer (ModelAssetAssimp import, Primitives). Returns false
        // on I/O failure. Skeleton/animation data is not cooked yet (static meshes only).
        static bool WriteToFile(const ModelAsset *model, const std::filesystem::path &file);

        static bool IsCookedPath(const std::filesystem::path &file);
    };
} // namespace pe
