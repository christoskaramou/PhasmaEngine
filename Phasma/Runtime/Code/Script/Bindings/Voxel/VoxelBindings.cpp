#include "Scene/SceneAccess.h"
#include "Scene/Scene.h"
#include "Script/ScriptSystem.h"
#include "Voxel/VoxelCollider.h"
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
                        if (p["lod0_radius"].valid())
                            cfg.lod0Radius = p["lod0_radius"];
                        if (p["world_radius"].valid())
                            cfg.worldRadius = p["world_radius"];
                        if (p["streaming"].valid())
                            cfg.streaming = p["streaming"];
                        if (p["save_dir"].valid())
                            cfg.saveDir = p["save_dir"].get<std::string>();
                        if (p["noise_amplitude"].valid())
                            cfg.noiseAmplitude = p["noise_amplitude"];
                        if (p["noise_feature_scale"].valid())
                            cfg.noiseFeatureScale = p["noise_feature_scale"];
                        if (p["noise_seed"].valid())
                            cfg.noiseSeed = p["noise_seed"];
                        if (p["caves"].valid())
                            cfg.caves = p["caves"];
                        if (p["sea_level"].valid())
                            cfg.seaLevel = p["sea_level"];
                        if (p["heightmap"].valid())
                            cfg.heightmapPath = p["heightmap"].get<std::string>();
                        if (p["strata1_map"].valid())
                            cfg.strata1Path = p["strata1_map"].get<std::string>();
                        if (p["strata2_map"].valid())
                            cfg.strata2Path = p["strata2_map"].get<std::string>();
                        if (p["blocks_per_pixel"].valid())
                            cfg.blocksPerPixel = p["blocks_per_pixel"];
                        if (p["surface_block"].valid())
                            cfg.surfaceBlock = p["surface_block"];
                        if (p["strata1_block"].valid())
                            cfg.strata1Block = p["strata1_block"];
                        if (p["strata2_block"].valid())
                            cfg.strata2Block = p["strata2_block"];
                        if (p["fill_block"].valid())
                            cfg.fillBlock = p["fill_block"];
                        if (p["strata1_thickness"].valid())
                            cfg.strata1Thickness = p["strata1_thickness"];
                        if (p["strata2_thickness"].valid())
                            cfg.strata2Thickness = p["strata2_thickness"];
                    }
                    vs->CreateWorld(scene, cfg);
                });

                voxel.set_function("save_all", []() -> bool {
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    if (!vs || !vs->World())
                        return false;
                    return vs->World()->SaveAllModified();
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
                });

                // Ray-pick a block. Returns {hit, cell={x,y,z}, adjacent={x,y,z}, normal={x,y,z}}.
                // adjacent is the empty cell on the near side of the hit face (the place target).
                voxel.set_function("raycast", [](float ox, float oy, float oz, float dx, float dy, float dz,
                                                 float maxDist, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table out = lua.create_table();
                    out["hit"] = false;
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    if (!vs || !vs->World())
                        return out;
                    pe::voxel::BlockPos cell{}, adjacent{};
                    vec3 normal(0.0f);
                    if (!vs->World()->Raycast(vec3(ox, oy, oz), vec3(dx, dy, dz), maxDist, cell, adjacent, normal))
                        return out;
                    // Block coords must be integers (Lua int subtype) so voxel.set_block(int...)
                    // accepts them; the normal is a float direction.
                    auto ivec = [&](int x, int y, int z) {
                        sol::table t = lua.create_table();
                        t["x"] = x; t["y"] = y; t["z"] = z;
                        return t;
                    };
                    out["hit"] = true;
                    out["cell"] = ivec(cell.x, cell.y, cell.z);
                    out["adjacent"] = ivec(adjacent.x, adjacent.y, adjacent.z);
                    sol::table n = lua.create_table();
                    n["x"] = normal.x; n["y"] = normal.y; n["z"] = normal.z;
                    out["normal"] = n;
                    return out;
                });

                // Swept-AABB move with per-axis slide against solid blocks. pos/half are the AABB
                // center + half-extents; returns the resolved center {x,y,z} after sliding.
                voxel.set_function("move_aabb", [](float px, float py, float pz, float hx, float hy, float hz,
                                                   float dx, float dy, float dz, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    vec3 pos(px, py, pz);
                    auto *vs = CreateGlobalSystem<pe::voxel::VoxelSystem>();
                    if (vs && vs->World())
                    {
                        auto *world = vs->World();
                        pos = pe::voxel::MoveAabb(pos, vec3(hx, hy, hz), vec3(dx, dy, dz),
                                                  [world](int x, int y, int z) {
                                                      return world->Registry().IsSolid(world->GetBlock(x, y, z));
                                                  });
                    }
                    else
                    {
                        pos += vec3(dx, dy, dz);
                    }
                    sol::table out = lua.create_table();
                    out["x"] = pos.x; out["y"] = pos.y; out["z"] = pos.z;
                    return out;
                }); });
        }
    } s_voxelBindings;
} // namespace pe
