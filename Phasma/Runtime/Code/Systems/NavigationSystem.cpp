#include "Systems/NavigationSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

#ifdef PE_PHYSICS
#include "Systems/PhysicsSystem.h"
#include "Script/ScriptRuntimeHooks.h"
#endif

namespace pe
{
    namespace
    {
        struct Soup
        {
            std::vector<float> &vertices;
            std::vector<int> &triangles;

            int Add(const vec3 &p)
            {
                vertices.insert(vertices.end(), {p.x, p.y, p.z});
                return static_cast<int>(vertices.size() / 3) - 1;
            }

            // A triangle of a convex shape, wound so its normal, cross(b - a, c - a) (Recast's), faces away from the
            // shape's centre: only then are a box's top and a sphere's cap walkable. Degenerate ones are dropped.
            void Outward(int a, int b, int c, const vec3 &centre)
            {
                const vec3 pa = At(a), pb = At(b), pc = At(c);
                const vec3 n = glm::cross(pb - pa, pc - pa);
                if (glm::dot(n, n) < 1e-12f)
                    return;
                const bool flip = glm::dot(n, (pa + pb + pc) / 3.0f - centre) < 0.0f;
                triangles.insert(triangles.end(), {a, flip ? c : b, flip ? b : c});
            }

            vec3 At(int i) const { return vec3(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]); }
        };

        // A profile of (radius, height) points, bottom to top, turned about `up` through `centre`.
        void Lathe(Soup &soup, const vec3 &centre, const vec3 &x, const vec3 &up, const vec3 &z,
                   const std::vector<vec2> &profile)
        {
            constexpr int kSegments = 16;
            std::vector<int> rings;
            for (const vec2 &p : profile)
                for (int s = 0; s < kSegments; ++s)
                {
                    const float a = 6.2831853f * static_cast<float>(s) / kSegments;
                    rings.push_back(soup.Add(centre + up * p.y + (x * std::cos(a) + z * std::sin(a)) * p.x));
                }
            for (size_t r = 0; r + 1 < profile.size(); ++r)
                for (int s = 0; s < kSegments; ++s)
                {
                    const int a = rings[r * kSegments + s], b = rings[r * kSegments + (s + 1) % kSegments];
                    const int c = rings[(r + 1) * kSegments + s], d = rings[(r + 1) * kSegments + (s + 1) % kSegments];
                    soup.Outward(a, b, d, centre);
                    soup.Outward(a, d, c, centre);
                }
        }

        std::vector<vec2> HemisphereProfile(float radius, float offset, bool top)
        {
            constexpr int kRings = 6;
            std::vector<vec2> profile;
            for (int k = 0; k <= kRings; ++k)
            {
                const float lat = 1.5707963f * static_cast<float>(top ? k : k - kRings) / kRings;
                profile.emplace_back(radius * std::cos(lat), radius * std::sin(lat) + offset);
            }
            return profile;
        }

        // A node's world matrix from the local matrices up its parent chain (current this frame, unlike the GPU copy).
        mat4 ComputeWorld(Scene &scene, NodeId *node)
        {
            mat4 world(1.0f);
            for (; node && scene.IsNodeAlive(node); node = scene.GetParent(node))
                world = scene.GetLocalMatrix(node) * world;
            return world;
        }
    } // namespace

    void NavigationSystem::CollectGeometry(Scene &scene, std::vector<float> &vertices, std::vector<int> &triangles)
    {
        vertices.clear();
        triangles.clear();
#ifdef PE_PHYSICS
        auto *physics = GetGlobalSystem<PhysicsSystem>();
        if (!physics)
            return;
        Soup soup{vertices, triangles};
        const auto &vertexStore = scene.GetVertexStore();
        const auto &indexStore = scene.GetIndexStore();
        for (uint32_t i = 0; i < scene.GetNodeCount(); ++i)
        {
            NodeId *node = scene.GetNodeId(i);
            // The same bodies the collider gizmos draw (SceneView DrawVolumeGizmos), static and solid only.
            if (!(scene.GetComponentFlags(node) & Component_Physics) || !scene.IsNodeHierarchyEnabled(node) ||
                scene.GetTriggerZoneForNode(node))
                continue;
            const PhysicsBodyDesc *desc = physics->GetBodyDesc(node);
            if (!desc || desc->bodyType != PhysicsBodyType::Static || desc->isTrigger)
                continue;
            const mat4 world = ComputeWorld(scene, node);
            const vec3 scale(glm::length(vec3(world[0])), glm::length(vec3(world[1])), glm::length(vec3(world[2])));
            const vec3 pos(world[3]);
            const vec3 ax = vec3(world[0]) / std::max(scale.x, 1e-6f);
            const vec3 ay = vec3(world[1]) / std::max(scale.y, 1e-6f);
            const vec3 az = vec3(world[2]) / std::max(scale.z, 1e-6f);
            const vec3 size = ScaledColliderSize(*desc, scale);
            switch (desc->shapeType)
            {
            case PhysicsShapeType::Box:
            {
                int corner[8];
                for (int c = 0; c < 8; ++c)
                    corner[c] = soup.Add(pos + ax * size.x * (c & 1 ? 1.0f : -1.0f) +
                                         ay * size.y * (c & 2 ? 1.0f : -1.0f) + az * size.z * (c & 4 ? 1.0f : -1.0f));
                constexpr int kFaces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
                for (const auto &f : kFaces)
                {
                    soup.Outward(corner[f[0]], corner[f[1]], corner[f[2]], pos);
                    soup.Outward(corner[f[0]], corner[f[2]], corner[f[3]], pos);
                }
                break;
            }
            case PhysicsShapeType::Sphere:
            {
                std::vector<vec2> profile = HemisphereProfile(size.x, 0.0f, false);
                for (const vec2 &p : HemisphereProfile(size.x, 0.0f, true))
                    if (p.y > 0.0f)
                        profile.push_back(p);
                Lathe(soup, pos, ax, ay, az, profile);
                break;
            }
            case PhysicsShapeType::Capsule: // Jolt capsules run along local Y: radius .x, half height .y
            {
                std::vector<vec2> profile = HemisphereProfile(size.x, -size.y, false);
                for (const vec2 &p : HemisphereProfile(size.x, size.y, true))
                    profile.push_back(p);
                Lathe(soup, pos, ax, ay, az, profile);
                break;
            }
            case PhysicsShapeType::ConvexHull: // ponytail: the mesh's own triangles, not its hull
            case PhysicsShapeType::Mesh:
            {
                // LOD0 triangles placed by the node's world matrix, as Jolt places the Mesh shape.
                const auto *refs = scene.GetNodeCache(node).meshRefs;
                if (!refs)
                    break;
                for (int meshRef : refs->meshRefs)
                {
                    if (meshRef < 0)
                        continue;
                    const auto &mesh = scene.GetMesh(meshRef); // auto: Mesh() names this class's accessor here
                    const int base = static_cast<int>(vertices.size() / 3);
                    for (uint32_t v = 0; v < mesh.vertexCount && mesh.vertexOffset + v < vertexStore.size(); ++v)
                    {
                        const auto &p = vertexStore[mesh.vertexOffset + v].position;
                        soup.Add(vec3(world * vec4(p[0], p[1], p[2], 1.0f)));
                    }
                    for (uint32_t t = 0; t + 2 < mesh.indexCount && mesh.indexOffset + t + 2 < indexStore.size(); t += 3)
                        for (uint32_t k = 0; k < 3; ++k)
                            triangles.push_back(base + static_cast<int>(indexStore[mesh.indexOffset + t + k]));
                }
                break;
            }
            }
        }
#endif
    }

    bool NavigationSystem::Bake(const NavMeshSettings &settings, std::string &error)
    {
        Scene *scene = GetActiveScene();
        if (!scene)
        {
            error = "no active scene";
            return false;
        }
        std::vector<float> vertices;
        std::vector<int> triangles;
        CollectGeometry(*scene, vertices, triangles);
        NavMesh baked; // a failed bake keeps the current mesh and its movers
        if (!baked.Build(vertices, triangles, settings, error))
        {
            PE_WARN("[Nav] bake failed: %s", error.c_str());
            return false;
        }
        DropMovers(); // a rebake invalidates every polygon they stand on
        m_mesh = std::move(baked);
        m_scene = scene;
        m_generation = scene->GetGeneration();
        m_mesh.DebugEdges(m_debugSegments);
        const NavMeshStats &stats = m_mesh.Stats();
        PE_INFO("[Nav] baked %d polygons from %zu triangles in %.1f ms", stats.polygons, triangles.size() / 3,
                stats.bakeMs);
        return true;
    }

    const std::vector<vec3> &NavigationSystem::DebugSegments() const
    {
        static const std::vector<vec3> none;
        return Mesh() ? m_debugSegments : none;
    }

    NavMeshSettings NavigationSystem::SceneSettingsForBake()
    {
        const auto &g = Settings::Get<SceneSettings>();
        NavMeshSettings s;
        s.agentRadius = g.nav_agent_radius;
        s.agentHeight = g.nav_agent_height;
        s.agentMaxClimb = g.nav_agent_climb;
        s.agentMaxSlope = g.nav_agent_slope;
        s.cellSize = g.nav_cell_size;
        s.cellHeight = g.nav_cell_height;
        return s;
    }

    const NavMesh *NavigationSystem::Mesh() const
    {
        const Scene *scene = GetActiveScene();
        return m_mesh.IsReady() && scene && scene == m_scene && scene->GetGeneration() == m_generation ? &m_mesh
                                                                                                       : nullptr;
    }

    void NavigationSystem::Destroy()
    {
        DropMovers();
        m_mesh.Clear();
    }

    void NavigationSystem::StopPlay()
    {
        DropMovers();
        m_paused = false;
    }

    void NavigationSystem::DropMovers()
    {
        m_agents.clear();
        m_members.clear();
        m_crowd.reset();
        m_swarm.reset();
    }

    bool NavigationSystem::ReadyForMovers(const char *what)
    {
        if (!IsScriptPlayMode())
        {
            PE_WARN("[Nav] %s: only during play", what);
            return false;
        }
        if (!Mesh())
        {
            PE_WARN("[Nav] %s: bake the navigation mesh first (nav.bake)", what);
            return false;
        }
        return true;
    }

    const NavigationSystem::Binding *NavigationSystem::Find(const std::vector<Binding> &bindings,
                                                            const SceneNodeHandle &node)
    {
        for (const Binding &b : bindings)
            if (b.node == node)
                return &b;
        return nullptr;
    }

    bool NavigationSystem::AddAgent(const SceneNodeHandle &node, const NavAgentParams &params)
    {
        if (!ReadyForMovers("add_agent") || !node.IsValid(*GetActiveScene()))
            return false;
        Remove(node);
        if (!m_crowd)
            m_crowd = std::make_unique<NavCrowd>(m_mesh);
        Scene *scene = GetActiveScene();
        const int index = m_crowd->Add(vec3(ComputeWorld(*scene, node.nodeId)[3]), params);
        if (index < 0)
        {
            PE_WARN("[Nav] add_agent: the node is off the mesh, or %d agents already walk", NavCrowd::kMaxAgents);
            return false;
        }
        m_agents.push_back({node, index});
        return true;
    }

    bool NavigationSystem::SetAgentTarget(const SceneNodeHandle &node, const vec3 &target)
    {
        const Binding *b = Find(m_agents, node);
        return b && m_crowd && m_crowd->SetTarget(b->index, target);
    }

    void NavigationSystem::StopAgent(const SceneNodeHandle &node)
    {
        if (const Binding *b = Find(m_agents, node); b && m_crowd)
            m_crowd->Stop(b->index);
    }

    bool NavigationSystem::AddSwarmMember(const SceneNodeHandle &node, const NavSwarmMember &member)
    {
        if (!ReadyForMovers("swarm_add") || !node.IsValid(*GetActiveScene()))
            return false;
        Remove(node);
        if (!m_swarm)
            m_swarm = std::make_unique<NavSwarm>(m_mesh);
        Scene *scene = GetActiveScene();
        const int index = m_swarm->Add(vec3(ComputeWorld(*scene, node.nodeId)[3]), member);
        if (index < 0)
        {
            PE_WARN("[Nav] swarm_add: the node is off the mesh");
            return false;
        }
        m_members.push_back({node, index});
        return true;
    }

    void NavigationSystem::SetSwarmTarget(const vec3 &target)
    {
        if (!m_swarm && ReadyForMovers("swarm_target"))
            m_swarm = std::make_unique<NavSwarm>(m_mesh);
        if (m_swarm)
            m_swarm->SetTarget(target);
    }

    void NavigationSystem::Remove(const SceneNodeHandle &node)
    {
        for (size_t i = 0; i < m_agents.size(); ++i)
            if (m_agents[i].node == node)
            {
                if (m_crowd)
                    m_crowd->Remove(m_agents[i].index);
                m_agents.erase(m_agents.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        for (size_t i = 0; i < m_members.size(); ++i)
            if (m_members[i].node == node)
            {
                if (m_swarm)
                    m_swarm->Remove(m_members[i].index);
                m_members.erase(m_members.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
    }

    bool NavigationSystem::GetVelocity(const SceneNodeHandle &node, vec3 &velocity) const
    {
        vec3 position;
        if (const Binding *b = Find(m_agents, node); b && m_crowd)
            return m_crowd->Get(b->index, position, velocity);
        if (const Binding *b = Find(m_members, node); b && m_swarm)
            return m_swarm->Get(b->index, position, velocity);
        return false;
    }

    bool NavigationSystem::IsAnchored(const SceneNodeHandle &node) const
    {
        const Binding *b = Find(m_members, node);
        return b && m_swarm && m_swarm->IsAnchored(b->index);
    }

    NavSwarmStats NavigationSystem::SwarmStats() const
    {
        return m_swarm ? m_swarm->Stats() : NavSwarmStats{};
    }

    void NavigationSystem::Place(Scene &scene, const SceneNodeHandle &node, const vec3 &world)
    {
        // World to the node's local space through its parent's world matrix, so a mover under a moved or scaled
        // parent still lands on the mesh.
        NodeId *parent = scene.GetParent(node.nodeId);
        const vec3 local = parent ? vec3(glm::inverse(ComputeWorld(scene, parent)) * vec4(world, 1.0f)) : world;
        mat4 m = scene.GetLocalMatrix(node.nodeId);
        m[3] = vec4(local, 1.0f);
        scene.SetLocalMatrix(node.nodeId, m);
    }

    void NavigationSystem::Update()
    {
        if (!IsScriptPlayMode() || m_paused || (m_agents.empty() && m_members.empty()))
            return;
        Scene *scene = GetActiveScene();
        if (!scene || !Mesh())
        {
            DropMovers(); // the scene changed under them
            return;
        }
        const float dt =
            static_cast<float>(FrameTimer::Instance().GetDelta()) * Settings::Get<SceneSettings>().time_scale;
        if (dt <= 0.0f)
            return;
        PE_PROFILE_SCOPE("Navigation");
        // A deleted node leaves the crowd or the swarm before anything moves.
        std::vector<SceneNodeHandle> gone;
        for (const auto *bindings : {&m_agents, &m_members})
            for (const Binding &b : *bindings)
                if (!b.node.IsValid(*scene))
                    gone.push_back(b.node);
        for (const SceneNodeHandle &node : gone)
            Remove(node);
        if (m_crowd && !m_agents.empty() && m_members.empty())
            m_crowd->Update(dt);
        if (m_swarm && !m_members.empty())
            m_swarm->Update(dt, m_crowd.get());
        vec3 position, velocity;
        for (const Binding &b : m_agents)
            if (m_crowd->Get(b.index, position, velocity))
                Place(*scene, b.node, position);
        for (const Binding &b : m_members)
            if (m_swarm->Get(b.index, position, velocity))
                Place(*scene, b.node, position);
    }
} // namespace pe
