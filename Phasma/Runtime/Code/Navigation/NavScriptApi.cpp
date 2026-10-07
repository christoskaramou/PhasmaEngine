#include "Navigation/NavScriptApi.h"
#include "Navigation/NavSwarm.h"

namespace pe::navscript
{
    namespace
    {
        // One handle space for meshes and swarms. A swarm points into its mesh, so a mesh goes with its swarms.
        struct Swarmed
        {
            uint32_t mesh = 0;
            std::unique_ptr<NavSwarm> swarm;
        };
        std::unordered_map<uint32_t, std::unique_ptr<NavMesh>> s_meshes;
        std::unordered_map<uint32_t, Swarmed> s_swarms;
        uint32_t s_last = 0;

        uint32_t NextHandle()
        {
            do
                ++s_last;
            while (s_last == 0 || s_meshes.count(s_last) || s_swarms.count(s_last));
            return s_last;
        }

        NavSwarm *Swarm(uint32_t swarm)
        {
            const auto it = s_swarms.find(swarm);
            return it == s_swarms.end() ? nullptr : it->second.swarm.get();
        }

        vec3 In(const phasma::Vec3 &v)
        {
            return vec3(v.x, v.y, v.z);
        }

        phasma::Vec3 Out(const vec3 &v)
        {
            return {v.x, v.y, v.z};
        }

        uint32_t Create(void *, const float *xyz, uint32_t vertexCount, const uint32_t *indices, uint32_t indexCount,
                        const phasma::NavBake *bake) noexcept
        {
            try
            {
                if (!xyz || !indices || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
                    return 0;
                const std::vector<float> vertices(xyz, xyz + static_cast<size_t>(vertexCount) * 3);
                std::vector<int> triangles;
                triangles.reserve(indexCount);
                for (uint32_t i = 0; i < indexCount; ++i)
                {
                    if (indices[i] >= vertexCount)
                        return 0;
                    triangles.push_back(static_cast<int>(indices[i]));
                }
                NavMeshSettings settings;
                if (bake)
                {
                    const auto pick = [](float value, float &field)
                    {
                        if (value != 0.0f)
                            field = value;
                    };
                    pick(bake->cellSize, settings.cellSize);
                    pick(bake->cellHeight, settings.cellHeight);
                    pick(bake->agentRadius, settings.agentRadius);
                    pick(bake->agentHeight, settings.agentHeight);
                    pick(bake->agentMaxClimb, settings.agentMaxClimb);
                    pick(bake->agentMaxSlope, settings.agentMaxSlope);
                }
                auto mesh = std::make_unique<NavMesh>();
                std::string error;
                if (!mesh->Build(vertices, triangles, settings, error))
                {
                    PE_WARN("[Nav] navCreate: %s", error.c_str());
                    return 0;
                }
                const uint32_t handle = NextHandle();
                s_meshes.emplace(handle, std::move(mesh));
                return handle;
            }
            catch (...)
            {
                return 0;
            }
        }

        void Destroy(void *, uint32_t mesh) noexcept
        {
            if (!s_meshes.count(mesh))
                return;
            for (auto it = s_swarms.begin(); it != s_swarms.end();)
                it = it->second.mesh == mesh ? s_swarms.erase(it) : std::next(it);
            s_meshes.erase(mesh);
        }

        uint32_t SwarmCreate(void *, uint32_t mesh) noexcept
        {
            try
            {
                const auto it = s_meshes.find(mesh);
                if (it == s_meshes.end())
                    return 0;
                const uint32_t handle = NextHandle();
                s_swarms.emplace(handle, Swarmed{mesh, std::make_unique<NavSwarm>(*it->second)});
                return handle;
            }
            catch (...)
            {
                return 0;
            }
        }

        void SwarmDestroy(void *, uint32_t swarm) noexcept
        {
            s_swarms.erase(swarm);
        }

        int32_t Add(void *, uint32_t handle, phasma::Vec3 position, float radius, float speed, float stop) noexcept
        {
            try
            {
                NavSwarm *swarm = Swarm(handle);
                return swarm ? swarm->Add(In(position), {radius, speed, stop}) : -1;
            }
            catch (...)
            {
                return -1;
            }
        }

        void Remove(void *, uint32_t handle, int32_t member) noexcept
        {
            if (NavSwarm *swarm = Swarm(handle))
                swarm->Remove(member);
        }

        void SetTarget(void *, uint32_t handle, phasma::Vec3 target) noexcept
        {
            try
            {
                if (NavSwarm *swarm = Swarm(handle))
                    swarm->SetTarget(In(target));
            }
            catch (...)
            {
            }
        }

        uint32_t SetSpeed(void *, uint32_t handle, int32_t member, float speed) noexcept
        {
            NavSwarm *swarm = Swarm(handle);
            return swarm && swarm->SetSpeed(member, speed);
        }

        uint32_t SetPosition(void *, uint32_t handle, int32_t member, phasma::Vec3 position) noexcept
        {
            NavSwarm *swarm = Swarm(handle);
            return swarm && swarm->SetPosition(member, In(position));
        }

        uint32_t Update(void *, uint32_t handle, float dt) noexcept
        {
            try
            {
                NavSwarm *swarm = Swarm(handle);
                return swarm ? static_cast<uint32_t>(swarm->Update(dt)) : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        uint32_t Get(void *, uint32_t handle, int32_t member, phasma::Vec3 *position, phasma::Vec3 *velocity,
                     phasma::Vec3 *drawn, uint32_t *anchored) noexcept
        {
            const NavSwarm *swarm = Swarm(handle);
            vec3 p, v, d;
            if (!swarm || !swarm->Get(member, p, v) || !swarm->GetInterpolated(member, d))
                return 0;
            if (position)
                *position = Out(p);
            if (velocity)
                *velocity = Out(v);
            if (drawn)
                *drawn = Out(d);
            if (anchored)
                *anchored = swarm->IsAnchored(member) ? 1u : 0u;
            return 1;
        }
    } // namespace

    void Fill(phasma::ScriptApi &api)
    {
        api.navCreate = &Create;
        api.navDestroy = &Destroy;
        api.navSwarmCreate = &SwarmCreate;
        api.navSwarmDestroy = &SwarmDestroy;
        api.navAdd = &Add;
        api.navRemove = &Remove;
        api.navSetTarget = &SetTarget;
        api.navSetSpeed = &SetSpeed;
        api.navSetPosition = &SetPosition;
        api.navUpdate = &Update;
        api.navGet = &Get;
    }

    void DestroyAll()
    {
        s_swarms.clear();
        s_meshes.clear();
    }
} // namespace pe::navscript
