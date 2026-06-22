#include "Scene/SceneAccess.h"
#include "Scene/Scene.h"
#include "Script/ScriptSystem.h"
#include "Voxel/VoxelSystem.h"

// Lua `voxel` table: create/destroy a voxel world on the active scene and read/write blocks.
// The VoxelSystem global is created idle at bootstrap; `voxel.create` builds the world on demand.
namespace pe
{
    static struct VoxelBindings
    {
        VoxelBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table voxel = lua.create_named_table("voxel");

                voxel.set_function("create", [](sol::optional<sol::table> params) {
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    Scene *scene = GetActiveScene();
                    if (!vs || !scene)
                        return;

                    pe::voxel::VoxelConfig cfg{};
                    if (params)
                    {
                        sol::table p = *params;
                        if (p["load_radius"].valid())
                            cfg.loadRadius = p["load_radius"];
                        if (p["unload_margin"].valid())
                            cfg.unloadMargin = p["unload_margin"];
                        if (p["ground_y"].valid())
                            cfg.groundY = p["ground_y"];
                        if (p["upload_budget"].valid())
                            cfg.uploadBudgetPerFrame = p["upload_budget"];
                    }
                    vs->CreateWorld(scene, cfg);
                });

                voxel.set_function("destroy", []() {
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    if (vs)
                        vs->Destroy();
                });

                voxel.set_function("set_anchor", [](float x, float y, float z) {
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    if (vs && vs->World())
                        vs->World()->SetAnchor(vec3(x, y, z));
                });

                voxel.set_function("get_block", [](int x, int y, int z) -> int {
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    if (!vs || !vs->World())
                        return 0;
                    return static_cast<int>(vs->World()->GetBlock(x, y, z));
                });

                voxel.set_function("set_block", [](int x, int y, int z, int id) {
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    if (vs && vs->World())
                        vs->World()->SetBlock(x, y, z, static_cast<pe::voxel::BlockId>(id));
                }); });
        }
    } s_voxelBindings;
} // namespace pe
