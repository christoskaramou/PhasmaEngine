#include "Navigation/NavDetail.h"

#ifdef PE_NAV
#include "DetourNavMeshBuilder.h"
#include "Recast.h"
#endif

namespace pe
{
#ifdef PE_NAV
    namespace
    {
        constexpr int kMaxPathPolys = 512;

        // Recast's build log into the engine log: a failed bake says why.
        class NavContext : public rcContext
        {
        protected:
            void doLog(const rcLogCategory category, const char *msg, const int) override
            {
                if (category == RC_LOG_ERROR || category == RC_LOG_WARNING)
                    PE_WARN("[Nav] %s", msg);
            }
        };

        template <class T, void (*Free)(T *)>
        struct Owned
        {
            T *p = nullptr;
            ~Owned()
            {
                if (p)
                    Free(p);
            }
        };
    } // namespace

    NavMesh::NavMesh() : m_impl(std::make_unique<Impl>()) {}
    NavMesh::~NavMesh() = default;

    void NavMesh::Clear()
    {
        m_impl->Reset();
        m_stats = {};
    }

    bool NavMesh::IsReady() const
    {
        return m_impl->query != nullptr;
    }

    bool NavMesh::Build(const std::vector<float> &vertices, const std::vector<int> &triangles,
                        const NavMeshSettings &s, std::string &error)
    {
        Clear();
        error.clear();
        const auto started = std::chrono::steady_clock::now();
        if (!std::isfinite(s.cellSize) || s.cellSize <= 0.0f || !std::isfinite(s.cellHeight) || s.cellHeight <= 0.0f ||
            !std::isfinite(s.agentRadius) || s.agentRadius < 0.0f || !std::isfinite(s.agentHeight) || s.agentHeight <= 0.0f ||
            !std::isfinite(s.agentMaxClimb) || s.agentMaxClimb < 0.0f || !std::isfinite(s.agentMaxSlope) ||
            s.agentMaxSlope < 0.0f || s.agentMaxSlope >= 90.0f || s.cellSize > kNavMaxCoordinate ||
            s.cellHeight > kNavMaxCoordinate || s.agentRadius > kNavMaxCoordinate || s.agentHeight > kNavMaxCoordinate ||
            s.agentMaxClimb > kNavMaxCoordinate ||
            static_cast<double>(s.agentHeight) / s.cellHeight > 255.0 || // compact span heights are 8 bits
            static_cast<double>(s.agentMaxClimb) / s.cellHeight > RC_SPAN_MAX_HEIGHT ||
            static_cast<double>(s.agentRadius) / s.cellSize > 127.0 || 12.0 / s.cellSize > std::numeric_limits<int>::max())
        {
            error = "invalid navigation settings: finite dimensions up to 1000000 m, positive cell sizes/height, nonnegative radius/climb, slope below 90, and supported voxel ratios required";
            return false;
        }
        if (vertices.size() % 3 || triangles.size() % 3 || vertices.size() / 3 > std::numeric_limits<int>::max() ||
            triangles.size() / 3 > std::numeric_limits<int>::max() ||
            !std::all_of(vertices.begin(), vertices.end(), [](float v)
                         { return std::isfinite(v) && std::abs(v) <= kNavMaxCoordinate; }))
        {
            error = "invalid navigation geometry: finite xyz within +/-1000000 metres and complete index triples required";
            return false;
        }
        const int nverts = static_cast<int>(vertices.size() / 3), ntris = static_cast<int>(triangles.size() / 3);
        if (nverts < 3 || ntris < 1)
        {
            error = "no geometry to bake (no static physics colliders?)";
            return false;
        }
        if (!std::all_of(triangles.begin(), triangles.end(), [nverts](int index)
                         { return index >= 0 && index < nverts; }))
        {
            error = "navigation triangle index is outside the vertex array";
            return false;
        }

        rcConfig cfg{};
        cfg.cs = s.cellSize;
        cfg.ch = s.cellHeight;
        cfg.walkableSlopeAngle = s.agentMaxSlope;
        cfg.walkableHeight = static_cast<int>(std::ceil(s.agentHeight / cfg.ch));
        cfg.walkableClimb = static_cast<int>(std::floor(s.agentMaxClimb / cfg.ch));
        cfg.walkableRadius = static_cast<int>(std::ceil(s.agentRadius / cfg.cs));
        cfg.maxEdgeLen = static_cast<int>(12.0f / cfg.cs);
        cfg.maxSimplificationError = 1.3f;
        cfg.minRegionArea = 8 * 8;
        cfg.mergeRegionArea = 20 * 20;
        cfg.maxVertsPerPoly = 6;
        cfg.detailSampleDist = cfg.cs * 6.0f;
        cfg.detailSampleMaxError = cfg.ch;
        rcCalcBounds(vertices.data(), nverts, cfg.bmin, cfg.bmax);
        const double width = (static_cast<double>(cfg.bmax[0]) - cfg.bmin[0]) / cfg.cs + 0.5;
        const double depth = (static_cast<double>(cfg.bmax[2]) - cfg.bmin[2]) / cfg.cs + 0.5;
        // ponytail: cap a solo tile at 16M cells; use tiled baking when a larger world needs this resolution.
        constexpr double kMaxCells = 16 * 1024 * 1024;
        // Recast and Detour also store vertices and BV bounds as 16-bit cell coordinates.
        if (width < 1 || depth < 1 || width > 0xffff || depth > 0xffff || width * depth > kMaxCells ||
            (static_cast<double>(cfg.bmax[1]) - cfg.bmin[1]) / cfg.ch > RC_SPAN_MAX_HEIGHT ||
            (static_cast<double>(cfg.bmax[1]) - cfg.bmin[1]) / cfg.cs > 0xffff)
        {
            error = "scene exceeds a solo nav tile's voxel limits: raise cell size/height or reduce the bake bounds";
            return false;
        }
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

        NavContext ctx;
        Owned<rcHeightfield, rcFreeHeightField> solid{rcAllocHeightfield()};
        std::vector<unsigned char> areas(static_cast<size_t>(ntris), 0);
        Owned<rcCompactHeightfield, rcFreeCompactHeightfield> chf{rcAllocCompactHeightfield()};
        Owned<rcContourSet, rcFreeContourSet> contours{rcAllocContourSet()};
        Owned<rcPolyMesh, rcFreePolyMesh> poly{rcAllocPolyMesh()};
        Owned<rcPolyMeshDetail, rcFreePolyMeshDetail> detail{rcAllocPolyMeshDetail()};
        if (!solid.p || !chf.p || !contours.p || !poly.p || !detail.p)
        {
            error = "Recast could not allocate the bake structures";
            return false;
        }
        if (!rcCreateHeightfield(&ctx, *solid.p, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
        {
            error = "the heightfield could not be allocated (scene too large for one tile?)";
            return false;
        }
        rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, vertices.data(), nverts, triangles.data(), ntris,
                                areas.data());
        if (!rcRasterizeTriangles(&ctx, vertices.data(), nverts, triangles.data(), areas.data(), ntris, *solid.p,
                                  cfg.walkableClimb))
        {
            error = "rasterizing the triangles failed";
            return false;
        }
        rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid.p);
        rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid.p);
        rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid.p);
        if (rcGetHeightFieldSpanCount(&ctx, *solid.p) > 0xffffff) // the compact heightfield indexes spans with 24 bits
        {
            error = "too many walkable spans for one nav tile: raise cell size/height or reduce the bake bounds";
            return false;
        }
        if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid.p, *chf.p) ||
            !rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf.p) || !rcBuildDistanceField(&ctx, *chf.p) ||
            !rcBuildRegions(&ctx, *chf.p, 0, cfg.minRegionArea, cfg.mergeRegionArea) ||
            !rcBuildContours(&ctx, *chf.p, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours.p) ||
            !rcBuildPolyMesh(&ctx, *contours.p, cfg.maxVertsPerPoly, *poly.p) ||
            !rcBuildPolyMeshDetail(&ctx, *poly.p, *chf.p, cfg.detailSampleDist, cfg.detailSampleMaxError, *detail.p))
        {
            error = "the Recast build failed (see the log)";
            return false;
        }
        if (poly.p->npolys == 0)
        {
            error = "nothing walkable: no upward faces wide enough for the agent";
            return false;
        }
        if (poly.p->nverts >= 0xffff)
        {
            error = "too many navmesh vertices for one tile (65535): raise the cell size";
            return false;
        }
        for (int i = 0; i < poly.p->npolys; ++i)
            poly.p->flags[i] = poly.p->areas[i] == RC_WALKABLE_AREA ? 1 : 0;

        dtNavMeshCreateParams params{};
        params.verts = poly.p->verts;
        params.vertCount = poly.p->nverts;
        params.polys = poly.p->polys;
        params.polyAreas = poly.p->areas;
        params.polyFlags = poly.p->flags;
        params.polyCount = poly.p->npolys;
        params.nvp = poly.p->nvp;
        params.detailMeshes = detail.p->meshes;
        params.detailVerts = detail.p->verts;
        params.detailVertsCount = detail.p->nverts;
        params.detailTris = detail.p->tris;
        params.detailTriCount = detail.p->ntris;
        params.walkableHeight = s.agentHeight;
        params.walkableRadius = s.agentRadius;
        params.walkableClimb = s.agentMaxClimb;
        rcVcopy(params.bmin, poly.p->bmin);
        rcVcopy(params.bmax, poly.p->bmax);
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.buildBvTree = true;
        unsigned char *data = nullptr;
        int size = 0;
        if (!dtCreateNavMeshData(&params, &data, &size))
        {
            error = "Detour could not pack the navmesh";
            return false;
        }
        m_impl->mesh = dtAllocNavMesh();
        if (!m_impl->mesh || dtStatusFailed(m_impl->mesh->init(data, size, DT_TILE_FREE_DATA)))
        {
            dtFree(data);
            m_impl->Reset();
            error = "Detour could not load the navmesh";
            return false;
        }
        m_impl->query = dtAllocNavMeshQuery();
        if (!m_impl->query || dtStatusFailed(m_impl->query->init(m_impl->mesh, 2048)))
        {
            m_impl->Reset();
            error = "Detour could not create a query";
            return false;
        }
        m_stats.polygons = poly.p->npolys;
        m_impl->settings = s;
        m_stats.vertices = poly.p->nverts;
        m_stats.bakeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
        return true;
    }

    bool NavMesh::FindPath(const vec3 &start, const vec3 &end, std::vector<vec3> &path, bool &complete) const
    {
        path.clear();
        complete = false;
        dtPolyRef startRef = 0, endRef = 0;
        vec3 from, to;
        if (!m_impl->Snap(start, startRef, &from.x) || !m_impl->Snap(end, endRef, &to.x))
            return false;
        dtPolyRef polys[kMaxPathPolys];
        int count = 0;
        const dtStatus found = m_impl->query->findPath(startRef, endRef, &from.x, &to.x, &m_impl->filter, polys, &count, kMaxPathPolys);
        if (dtStatusFailed(found) || count == 0)
            return false;
        if (dtStatusDetail(found, DT_OUT_OF_NODES) || dtStatusDetail(found, DT_BUFFER_TOO_SMALL))
            PE_WARN("[Nav] find_path hit its search limit: the route may stop short of a reachable end");
        complete = polys[count - 1] == endRef;
        if (!complete) // stop at the reachable point nearest the end
            m_impl->query->closestPointOnPoly(polys[count - 1], &to.x, &to.x, nullptr);
        float corners[kMaxPathPolys * 3];
        int cornerCount = 0;
        const dtStatus status = m_impl->query->findStraightPath(&from.x, &to.x, polys, count, corners, nullptr, nullptr, &cornerCount,
                                                                kMaxPathPolys);
        if (dtStatusFailed(status))
            return false;
        complete = complete && !dtStatusDetail(status, DT_BUFFER_TOO_SMALL);
        path.reserve(static_cast<size_t>(cornerCount));
        for (int i = 0; i < cornerCount; ++i)
            path.emplace_back(corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2]);
        return !path.empty();
    }

    bool NavMesh::NearestPoint(const vec3 &point, vec3 &out) const
    {
        dtPolyRef ref = 0;
        return m_impl->Snap(point, ref, &out.x);
    }

    bool NavMesh::Raycast(const vec3 &start, const vec3 &end, bool &blocked, vec3 &hit) const
    {
        blocked = false;
        dtPolyRef ref = 0;
        vec3 from;
        if (!ValidNavPoint(end) || !m_impl->Snap(start, ref, &from.x))
            return false;
        float t = 0.0f, normal[3];
        dtPolyRef polys[kMaxPathPolys];
        int count = 0;
        if (dtStatusFailed(m_impl->query->raycast(ref, &from.x, &end.x, &m_impl->filter, &t, normal, polys, &count, kMaxPathPolys)))
            return false;
        blocked = t <= 1.0f; // FLT_MAX: the walk reached `end`
        if (blocked)
        {
            hit = from + (end - from) * t;
            if (count > 0) // Detour walks in 2D: the height comes from the mesh
                m_impl->query->closestPointOnPoly(polys[count - 1], &hit.x, &hit.x, nullptr);
        }
        return true;
    }

    void NavMesh::DebugEdges(std::vector<vec3> &segments) const
    {
        segments.clear();
        const dtNavMesh *mesh = m_impl->mesh;
        if (!mesh)
            return;
        for (int i = 0; i < mesh->getMaxTiles(); ++i)
        {
            const dtMeshTile *tile = mesh->getTile(i);
            if (!tile || !tile->header)
                continue;
            for (int p = 0; p < tile->header->polyCount; ++p)
            {
                const dtPoly &poly = tile->polys[p];
                for (int j = 0; j < poly.vertCount; ++j)
                {
                    const float *a = &tile->verts[poly.verts[j] * 3];
                    const float *b = &tile->verts[poly.verts[(j + 1) % poly.vertCount] * 3];
                    segments.emplace_back(a[0], a[1], a[2]);
                    segments.emplace_back(b[0], b[1], b[2]);
                }
            }
        }
    }
#else
    struct NavMesh::Impl
    {
    };

    NavMesh::NavMesh() : m_impl(std::make_unique<Impl>()) {}
    NavMesh::~NavMesh() = default;
    void NavMesh::Clear()
    {
        m_stats = {};
    }
    bool NavMesh::IsReady() const
    {
        return false;
    }

    bool NavMesh::Build(const std::vector<float> &, const std::vector<int> &, const NavMeshSettings &,
                        std::string &error)
    {
        error = "this build has no navigation (PE_NAV is off)";
        return false;
    }

    bool NavMesh::FindPath(const vec3 &, const vec3 &, std::vector<vec3> &path, bool &complete) const
    {
        path.clear();
        complete = false;
        return false;
    }

    bool NavMesh::NearestPoint(const vec3 &, vec3 &) const
    {
        return false;
    }
    bool NavMesh::Raycast(const vec3 &, const vec3 &, bool &blocked, vec3 &) const
    {
        blocked = false;
        return false;
    }
    void NavMesh::DebugEdges(std::vector<vec3> &segments) const
    {
        segments.clear();
    }
#endif

    NavMesh::NavMesh(NavMesh &&) noexcept = default;
    NavMesh &NavMesh::operator=(NavMesh &&) noexcept = default;
} // namespace pe
