#pragma once

#include "Navigation/NavMesh.h"

namespace pe
{
    struct NavAgentParams
    {
        float radius = 0.4f;
        float height = 1.8f;
        float maxSpeed = 3.5f;        // metres per second
        float maxAcceleration = 8.0f; // metres per second squared
    };

    // Path-following agents on Detour's crowd: each walks the mesh to its own target and steers around the others by
    // sampling velocities ahead of time (no separation push; Detour's own overlap resolution remains for bodies that
    // start inside each other). NavSwarm::Update(dt, &crowd) shares avoidance when both kinds of mover coexist;
    // then do not also call Update here. Built on a baked NavMesh, which must outlive it and not be rebaked under it.
    class NavCrowd
    {
    public:
        static constexpr int kMaxAgents = 256;
        static constexpr float kMaxAgentRadius = 2.0f; // Detour fixes the largest radius when the crowd is created

        explicit NavCrowd(const NavMesh &mesh);
        ~NavCrowd();
        NavCrowd(const NavCrowd &) = delete;
        NavCrowd &operator=(const NavCrowd &) = delete;

        // The agent's index, or -1 when the position is off the mesh or the crowd is full.
        int Add(const vec3 &position, const NavAgentParams &params);
        void Remove(int agent);
        // False when the target is off the mesh.
        bool SetTarget(int agent, const vec3 &target);
        void Stop(int agent);
        void Update(float dt);
        bool Get(int agent, vec3 &position, vec3 &velocity) const;
        // Where to draw the agent: Get's position, except while a swarm steps the crowd at 60 Hz, when it is blended
        // between the last two steps by the time since (one step behind, smooth at any frame rate).
        bool GetInterpolated(int agent, vec3 &position) const;

    private:
        friend class NavSwarm;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace pe
