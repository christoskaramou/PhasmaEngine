#include "Scene/SceneAccess.h"
#include "Scene/Scene.h"
#include "Script/ScriptSystem.h"
#include "Terrain/TerrainSystem.h"

// Lua `terrain` table: runtime queries + a height brush for the scene's heightfield Terrain node.
// The TerrainSystem global is created idle at bootstrap and reconciles the Terrain node; these read
// its live world. Authoring (extent, heightmap, physics) is done on the node via node:set_terrain.
namespace pe
{
    static struct TerrainBindings
    {
        TerrainBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table terrain = lua.create_named_table("terrain");

                // Height brush: raise (amount > 0) or lower (amount < 0) the surface within radius at
                // world (x, z), then re-mesh. Deferred to the next system tick (safe from script update).
                terrain.set_function("sculpt", [](float x, float z, float radius, float amount) {
                    if (auto *ts = GetGlobalSystem<terrain::TerrainSystem>())
                        if (ts->World())
                            ts->World()->QueueSculpt(vec3(x, 0.0f, z), radius, amount);
                });

                // Surface world-Y at (x, z). Very low when there is no terrain.
                terrain.set_function("height", [](float x, float z) -> float {
                    auto *ts = GetGlobalSystem<terrain::TerrainSystem>();
                    return (ts && ts->World()) ? ts->World()->SampleHeight(x, z) : -1e9f;
                });

                // Ray-march the terrain surface. Returns {hit, point={x,y,z}, normal={x,y,z}}; aim a ray
                // down for ground height under a point, or along the view for a target.
                terrain.set_function("raycast", [](float ox, float oy, float oz, float dx, float dy, float dz,
                                                   float maxDist, sol::this_state st) -> sol::table {
                    sol::state_view sv(st);
                    sol::table out = sv.create_table();
                    out["hit"] = false;
                    auto *ts = GetGlobalSystem<terrain::TerrainSystem>();
                    if (!ts || !ts->World())
                        return out;
                    vec3 pt(0.0f), n(0.0f);
                    if (!ts->World()->Raycast(vec3(ox, oy, oz), vec3(dx, dy, dz), maxDist, pt, n))
                        return out;
                    auto fvec = [&](const vec3 &v) {
                        sol::table t = sv.create_table();
                        t["x"] = v.x; t["y"] = v.y; t["z"] = v.z;
                        return t;
                    };
                    out["hit"] = true;
                    out["point"] = fvec(pt);
                    out["normal"] = fvec(n);
                    return out;
                }); });
        }
    } s_terrainBindings;
} // namespace pe
