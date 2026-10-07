#include "Navigation/NavCrowd.h"
#include "Navigation/NavDetail.h"

#ifdef PE_NAV
#include "DetourCrowd.h"
#endif

namespace pe
{
#ifdef PE_NAV
    NavCrowd::NavCrowd(const NavMesh &mesh) : m_impl(std::make_unique<Impl>())
    {
        m_impl->nav = mesh.m_impl.get();
        if (!m_impl->nav->mesh)
            return;
        m_impl->crowd = dtAllocCrowd();
        if (m_impl->crowd && !m_impl->crowd->init(kMaxAgents, kMaxAgentRadius, m_impl->nav->mesh))
        {
            dtFreeCrowd(m_impl->crowd);
            m_impl->crowd = nullptr;
        }
        if (!m_impl->crowd)
            PE_WARN("[Nav] the crowd could not be created");
    }

    NavCrowd::~NavCrowd() = default;

    int NavCrowd::Add(const vec3 &position, const NavAgentParams &params)
    {
        if (!std::isfinite(params.radius) || params.radius <= 0.0f || params.radius > kMaxAgentRadius ||
            !std::isfinite(params.height) || params.height <= 0.0f || !std::isfinite(params.maxSpeed) || params.maxSpeed < 0.0f ||
            !std::isfinite(params.maxAcceleration) || params.maxAcceleration < 0.0f ||
            params.maxSpeed > kNavMaxSpeed || params.maxAcceleration > 1e6f ||
            params.radius > m_impl->nav->settings.agentRadius || params.height > m_impl->nav->settings.agentHeight)
        {
            PE_WARN("[Nav] invalid agent dimensions/speed/acceleration, or agent larger than the baked size");
            return -1;
        }
        dtPolyRef ref = 0;
        vec3 start;
        if (!m_impl->crowd || !m_impl->nav->Snap(position, ref, &start.x))
            return -1;
        dtCrowdAgentParams p{};
        p.radius = params.radius;
        p.height = params.height;
        p.maxAcceleration = params.maxAcceleration;
        p.maxSpeed = params.maxSpeed;
        p.collisionQueryRange = p.radius * 12.0f;
        p.pathOptimizationRange = p.radius * 30.0f;
        // Avoidance by velocity sampling, no DT_CROWD_SEPARATION: agents steer around each other instead of pushing.
        p.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_OPTIMIZE_VIS |
                        DT_CROWD_OPTIMIZE_TOPO;
        p.obstacleAvoidanceType = 0; // dtCrowd::init gives every slot the same default sampling
        p.separationWeight = 0.0f;
        const int index = m_impl->crowd->addAgent(&start.x, &p);
        if (index >= 0)
            m_impl->previous[index] = start;
        return index;
    }

    void NavCrowd::Remove(int agent)
    {
        if (m_impl->crowd && agent >= 0 && agent < kMaxAgents)
            m_impl->crowd->removeAgent(agent);
    }

    bool NavCrowd::SetTarget(int agent, const vec3 &target)
    {
        dtPolyRef ref = 0;
        vec3 point;
        return m_impl->crowd && agent >= 0 && agent < kMaxAgents && m_impl->crowd->getAgent(agent)->active &&
               m_impl->nav->Snap(target, ref, &point.x) &&
               m_impl->crowd->requestMoveTarget(agent, ref, &point.x);
    }

    void NavCrowd::Stop(int agent)
    {
        if (m_impl->crowd && agent >= 0 && agent < kMaxAgents)
            m_impl->crowd->resetMoveTarget(agent);
    }

    void NavCrowd::Update(float dt)
    {
        if (!m_impl->crowd || !std::isfinite(dt) || dt <= 0.0f)
            return;
        // ponytail: drop time beyond a 250 ms hitch; use fixed substeps if low-frame-rate simulation is required.
        m_impl->crowd->update(std::min(dt, 0.25f), nullptr);
        m_impl->alpha = 1.0f; // drawn where it is; a swarm stepping it later starts from here
        for (int i = 0; i < kMaxAgents; ++i)
        {
            const dtCrowdAgent *a = m_impl->crowd->getAgent(i);
            if (a->active)
                m_impl->previous[i] = vec3(a->npos[0], a->npos[1], a->npos[2]);
        }
    }

    bool NavCrowd::Get(int agent, vec3 &position, vec3 &velocity) const
    {
        const dtCrowdAgent *a = m_impl->crowd && agent >= 0 && agent < kMaxAgents ? m_impl->crowd->getAgent(agent) : nullptr;
        if (!a || !a->active)
            return false;
        position = vec3(a->npos[0], a->npos[1], a->npos[2]);
        velocity = vec3(a->vel[0], a->vel[1], a->vel[2]);
        return true;
    }

    bool NavCrowd::GetInterpolated(int agent, vec3 &position) const
    {
        vec3 velocity;
        if (!Get(agent, position, velocity))
            return false;
        position = glm::mix(m_impl->previous[agent], position, m_impl->alpha);
        return true;
    }
#else
    struct NavCrowd::Impl
    {
    };

    NavCrowd::NavCrowd(const NavMesh &) : m_impl(std::make_unique<Impl>()) {}
    NavCrowd::~NavCrowd() = default;
    int NavCrowd::Add(const vec3 &, const NavAgentParams &)
    {
        return -1;
    }
    void NavCrowd::Remove(int) {}
    bool NavCrowd::SetTarget(int, const vec3 &)
    {
        return false;
    }
    void NavCrowd::Stop(int) {}
    void NavCrowd::Update(float) {}
    bool NavCrowd::Get(int, vec3 &, vec3 &) const
    {
        return false;
    }
    bool NavCrowd::GetInterpolated(int, vec3 &) const
    {
        return false;
    }
#endif
} // namespace pe
