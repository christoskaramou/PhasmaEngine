#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNodeHandle.h"
#include "Script/ScriptSystem.h"
#include "Systems/NavigationSystem.h"

namespace pe
{
    namespace
    {
        // Bake settings: the scene's nav_* settings, overridden by an optional table
        // { radius, height, climb, slope (degrees), cell_size, cell_height }.
        NavMeshSettings ReadNavSettings(const sol::object &options)
        {
            NavMeshSettings s = NavigationSystem::SceneSettingsForBake();
            if (options.get_type() != sol::type::table)
                return s;
            const sol::table t = options.as<sol::table>();
            s.agentRadius = t.get_or("radius", s.agentRadius);
            s.agentHeight = t.get_or("height", s.agentHeight);
            s.agentMaxClimb = t.get_or("climb", s.agentMaxClimb);
            s.agentMaxSlope = t.get_or("slope", s.agentMaxSlope);
            s.cellSize = t.get_or("cell_size", s.cellSize);
            s.cellHeight = t.get_or("cell_height", s.cellHeight);
            return s;
        }

        sol::table PointTable(sol::state_view lua, const vec3 &p)
        {
            sol::table t = lua.create_table();
            t["x"] = p.x;
            t["y"] = p.y;
            t["z"] = p.z;
            return t;
        }

        const NavMesh *ActiveMesh()
        {
            auto *nav = GetGlobalSystem<NavigationSystem>();
            return nav ? nav->Mesh() : nullptr;
        }

        // The navigation system when `node` is a live node of the active scene.
        NavigationSystem *ForNode(const SceneNodeHandle &node)
        {
            Scene *scene = GetActiveScene();
            return scene && node.IsValid(*scene) ? GetGlobalSystem<NavigationSystem>() : nullptr;
        }

        // { radius, height, speed, accel } over the defaults.
        NavAgentParams ReadAgentParams(const sol::object &options)
        {
            NavAgentParams p;
            if (options.get_type() != sol::type::table)
                return p;
            const sol::table t = options.as<sol::table>();
            p.radius = t.get_or("radius", p.radius);
            p.height = t.get_or("height", p.height);
            p.maxSpeed = t.get_or("speed", p.maxSpeed);
            p.maxAcceleration = t.get_or("accel", p.maxAcceleration);
            return p;
        }

        // { radius, speed, stop } over the defaults.
        NavSwarmMember ReadMemberParams(const sol::object &options)
        {
            NavSwarmMember m;
            if (options.get_type() != sol::type::table)
                return m;
            const sol::table t = options.as<sol::table>();
            m.radius = t.get_or("radius", m.radius);
            m.speed = t.get_or("speed", m.speed);
            m.stopDistance = t.get_or("stop", m.stopDistance);
            return m;
        }
    } // namespace

    static struct NavBindings
    {
        NavBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table nav = lua.create_named_table("nav");
                // nav.bake([settings]) -> { polygons, vertices, ms } | nil, error
                nav.set_function("bake", [](sol::object options, sol::this_state ts) -> std::tuple<sol::object, sol::object> {
                    sol::state_view lua(ts);
                    auto *system = GetGlobalSystem<NavigationSystem>();
                    std::string error = "navigation is unavailable";
                    if (system && system->Bake(ReadNavSettings(options), error))
                    {
                        const NavMeshStats &stats = system->Mesh()->Stats();
                        sol::table t = lua.create_table();
                        t["polygons"] = stats.polygons;
                        t["vertices"] = stats.vertices;
                        t["ms"] = stats.bakeMs;
                        return {sol::make_object(lua, t), sol::make_object(lua, sol::lua_nil)};
                    }
                    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, error)};
                });
                nav.set_function("is_ready", []() -> bool { return ActiveMesh() != nullptr; });
                // nav.find_path(ax, ay, az, bx, by, bz) -> { points = { {x, y, z}, ... }, complete } | nil off the mesh
                nav.set_function("find_path", [](float ax, float ay, float az, float bx, float by, float bz,
                                                 sol::this_state ts) -> sol::object {
                    const NavMesh *mesh = ActiveMesh();
                    std::vector<vec3> path;
                    bool complete = false;
                    if (!mesh || !mesh->FindPath(vec3(ax, ay, az), vec3(bx, by, bz), path, complete))
                        return sol::nil;
                    sol::state_view lua(ts);
                    sol::table points = lua.create_table(static_cast<int>(path.size()), 0);
                    for (size_t i = 0; i < path.size(); ++i)
                        points[i + 1] = PointTable(lua, path[i]);
                    sol::table t = lua.create_table();
                    t["points"] = points;
                    t["complete"] = complete;
                    return t;
                });
                // nav.nearest(x, y, z) -> {x, y, z} on the mesh | nil
                nav.set_function("nearest", [](float x, float y, float z, sol::this_state ts) -> sol::object {
                    const NavMesh *mesh = ActiveMesh();
                    vec3 out;
                    if (!mesh || !mesh->NearestPoint(vec3(x, y, z), out))
                        return sol::nil;
                    return PointTable(sol::state_view(ts), out);
                });
                // nav.raycast(ax, ay, az, bx, by, bz) -> {x, y, z} where a mesh edge stops the walk | false when clear | nil off the mesh
                nav.set_function("raycast", [](float ax, float ay, float az, float bx, float by, float bz,
                                               sol::this_state ts) -> sol::object {
                    const NavMesh *mesh = ActiveMesh();
                    bool blocked = false;
                    vec3 hit;
                    if (!mesh || !mesh->Raycast(vec3(ax, ay, az), vec3(bx, by, bz), blocked, hit))
                        return sol::nil;
                    if (!blocked)
                        return sol::make_object(sol::state_view(ts), false);
                    return PointTable(sol::state_view(ts), hit);
                });

                // Movers (play only, on a baked mesh): the node is placed on the mesh every frame.
                // nav.add_agent(node, [{radius, height, speed, accel}]) -> bool: a path-following agent
                nav.set_function("add_agent", [](SceneNodeHandle &node, sol::object options) -> bool {
                    auto *nav = ForNode(node);
                    return nav && nav->AddAgent(node, ReadAgentParams(options));
                });
                // nav.set_target(node, x, y, z) -> bool: walk there (false off the mesh or not an agent)
                nav.set_function("set_target", [](SceneNodeHandle &node, float x, float y, float z) -> bool {
                    auto *nav = ForNode(node);
                    return nav && nav->SetAgentTarget(node, vec3(x, y, z));
                });
                nav.set_function("stop", [](SceneNodeHandle &node) {
                    if (auto *nav = ForNode(node))
                        nav->StopAgent(node);
                });
                // nav.swarm_add(node, [{radius, speed, stop}]) -> bool: join the scene's swarm
                nav.set_function("swarm_add", [](SceneNodeHandle &node, sol::object options) -> bool {
                    auto *nav = ForNode(node);
                    return nav && nav->AddSwarmMember(node, ReadMemberParams(options));
                });
                // nav.swarm_target(x, y, z): what the whole swarm chases
                nav.set_function("swarm_target", [](float x, float y, float z) {
                    if (auto *nav = GetGlobalSystem<NavigationSystem>())
                        nav->SetSwarmTarget(vec3(x, y, z));
                });
                // nav.remove(node): stop moving the node (an agent or a swarm member)
                nav.set_function("remove", [](SceneNodeHandle &node) {
                    if (auto *nav = ForNode(node))
                        nav->Remove(node);
                });
                // nav.get_velocity(node) -> {x, y, z} | nil when the node is not a mover
                nav.set_function("get_velocity", [](SceneNodeHandle &node, sol::this_state ts) -> sol::object {
                    auto *nav = ForNode(node);
                    vec3 velocity;
                    if (!nav || !nav->GetVelocity(node, velocity))
                        return sol::nil;
                    return PointTable(sol::state_view(ts), velocity);
                });
                // nav.is_anchored(node) -> bool: a swarm member holding at its stop distance
                nav.set_function("is_anchored", [](SceneNodeHandle &node) -> bool {
                    auto *nav = ForNode(node);
                    return nav && nav->IsAnchored(node);
                });
                // nav.swarm_stats() -> {members, steps, ms}: the last frame's fixed steps and their mean cost
                nav.set_function("swarm_stats", [](sol::this_state ts) -> sol::table {
                    auto *nav = GetGlobalSystem<NavigationSystem>();
                    const NavSwarmStats stats = nav ? nav->SwarmStats() : NavSwarmStats{};
                    sol::table t = sol::state_view(ts).create_table();
                    t["members"] = stats.members;
                    t["steps"] = stats.steps;
                    t["ms"] = stats.stepMs;
                    return t;
                }); });
        }
    } s_navBindings;
} // namespace pe
