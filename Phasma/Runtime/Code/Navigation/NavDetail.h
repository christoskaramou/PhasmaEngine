#pragma once

// Private to the Navigation module: the Detour state behind NavMesh, shared by NavMesh.cpp, NavCrowd.cpp and
// NavSwarm.cpp. Never include this from a public header (Recast/Detour link PRIVATE into PhasmaRuntime).

#include "Navigation/NavMesh.h"
#include "Navigation/NavCrowd.h"

#ifdef PE_NAV
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourCrowd.h"

namespace pe
{
    inline constexpr float kNavExtents[3] = {2.0f, 4.0f, 2.0f}; // how far off the mesh a query point may lie
    inline constexpr float kNavMaxCoordinate = 1e6f;
    inline constexpr float kNavMaxSpeed = 1e4f;

    inline bool ValidNavPoint(const vec3 &p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= kNavMaxCoordinate &&
               std::abs(p.y) <= kNavMaxCoordinate && std::abs(p.z) <= kNavMaxCoordinate;
    }

    struct NavMesh::Impl
    {
        dtNavMesh *mesh = nullptr;
        dtNavMeshQuery *query = nullptr;
        dtQueryFilter filter;
        NavMeshSettings settings;

        Impl() { filter.setIncludeFlags(0xffff); }
        ~Impl() { Reset(); }

        void Reset()
        {
            dtFreeNavMeshQuery(query);
            dtFreeNavMesh(mesh);
            query = nullptr;
            mesh = nullptr;
        }

        bool Snap(const vec3 &p, dtPolyRef &ref, float out[3]) const
        {
            ref = 0;
            return query && ValidNavPoint(p) && dtStatusSucceed(query->findNearestPoly(&p.x, kNavExtents, &filter, &ref, out)) && ref;
        }
    };

    struct NavCrowd::Impl
    {
        const NavMesh::Impl *nav = nullptr;
        dtCrowd *crowd = nullptr;

        ~Impl() { dtFreeCrowd(crowd); }
    };
} // namespace pe
#endif
