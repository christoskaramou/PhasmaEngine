#include "Navigation/NavSwarm.h"
#include "Navigation/NavDetail.h"

namespace pe
{
#ifdef PE_NAV
    namespace
    {
        constexpr float kHorizon = 0.6f;   // seconds the avoidance looks ahead
        constexpr int kMaxNeighbours = 10; // the nearest ones only
        constexpr float kFloorGap = 2.0f;  // members further apart in height are on different floors
        constexpr float kEpsilon = 1e-5f;
        // Avoid as if 10% wider: packed tight, three-way squeezes and wall clamps leave a few cm of overlap at the
        // true radii; the margin absorbs it without any push.
        constexpr float kAvoidMargin = 1.1f;
        constexpr float kAnchorHysteresis = 0.3f; // an anchored member lets go only this far past its stop line
        constexpr float kEdgeLookAhead = 1.0f;    // this close to an exit edge, a member aims one polygon further
        // A member slower than this share of its speed, within this many avoidance widths of a held body in its way,
        // holds too: the ones behind a full stop line settle instead of jostling for it.
        constexpr float kSettleSpeed = 0.25f;
        constexpr float kSettleReach = 1.2f;
        // Walls are gathered this far around each polygon, plus the widest member's radius beyond the bake's: enough
        // for a step at up to 15 m/s.
        constexpr float kWallReach = 0.25f;

        // ORCA (van den Berg et al., "Reciprocal n-body collision avoidance"): each neighbour bounds the velocity to
        // one side of a line; the velocity nearest the preferred one inside every half-plane and the speed limit wins.
        // A velocity v satisfies the line when Det(direction, point - v) <= 0.
        struct Line
        {
            vec2 point{0.0f}, direction{0.0f};
        };

        float Det(const vec2 &a, const vec2 &b)
        {
            return a.x * b.y - a.y * b.x;
        }

        // The best point on line `index` that satisfies lines [0, index) and the speed circle.
        bool LinearProgram1(const std::vector<Line> &lines, size_t index, float radius, const vec2 &optimal,
                            bool directionOpt, vec2 &result)
        {
            const Line &line = lines[index];
            const float dot = glm::dot(line.point, line.direction);
            const float discriminant = dot * dot + radius * radius - glm::dot(line.point, line.point);
            if (discriminant < 0.0f)
                return false; // the speed circle misses the line
            const float root = std::sqrt(discriminant);
            float left = -dot - root, right = -dot + root;
            for (size_t i = 0; i < index; ++i)
            {
                const float denominator = Det(line.direction, lines[i].direction);
                const float numerator = Det(lines[i].direction, line.point - lines[i].point);
                if (std::abs(denominator) <= kEpsilon)
                {
                    if (numerator < 0.0f)
                        return false; // parallel and on the wrong side
                    continue;
                }
                const float t = numerator / denominator;
                if (denominator >= 0.0f)
                    right = std::min(right, t);
                else
                    left = std::max(left, t);
                if (left > right)
                    return false;
            }
            if (directionOpt)
                result = line.point + (glm::dot(optimal, line.direction) > 0.0f ? right : left) * line.direction;
            else
                result = line.point + std::clamp(glm::dot(line.direction, optimal - line.point), left, right) *
                                          line.direction;
            return true;
        }

        // The velocity nearest `optimal` satisfying every line; returns the first line it could not satisfy.
        size_t LinearProgram2(const std::vector<Line> &lines, float radius, const vec2 &optimal, bool directionOpt,
                              vec2 &result)
        {
            if (directionOpt)
                result = optimal * radius;
            else if (glm::dot(optimal, optimal) > radius * radius)
                result = glm::normalize(optimal) * radius;
            else
                result = optimal;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (Det(lines[i].direction, lines[i].point - result) > 0.0f)
                {
                    const vec2 previous = result;
                    if (!LinearProgram1(lines, i, radius, optimal, directionOpt, result))
                    {
                        result = previous;
                        return i;
                    }
                }
            }
            return lines.size();
        }

        // Infeasible (packed too tight): the velocity that violates the lines least, from line `begin` on; the first
        // `walls` lines stay hard.
        void LinearProgram3(const std::vector<Line> &lines, size_t walls, size_t begin, float radius, vec2 &result,
                            std::vector<Line> &projected)
        {
            float distance = 0.0f;
            for (size_t i = begin; i < lines.size(); ++i)
            {
                if (Det(lines[i].direction, lines[i].point - result) <= distance)
                    continue;
                projected.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(walls));
                for (size_t j = walls; j < i; ++j)
                {
                    Line line;
                    const float determinant = Det(lines[i].direction, lines[j].direction);
                    if (std::abs(determinant) <= kEpsilon)
                    {
                        if (glm::dot(lines[i].direction, lines[j].direction) > 0.0f)
                            continue; // same direction
                        line.point = 0.5f * (lines[i].point + lines[j].point);
                    }
                    else
                    {
                        line.point = lines[i].point +
                                     (Det(lines[j].direction, lines[i].point - lines[j].point) / determinant) *
                                         lines[i].direction;
                    }
                    line.direction = glm::normalize(lines[j].direction - lines[i].direction);
                    projected.push_back(line);
                }
                const vec2 previous = result;
                if (LinearProgram2(projected, radius, vec2(-lines[i].direction.y, lines[i].direction.x), true,
                                   result) < projected.size())
                    result = previous; // numerical noise; keep the last answer
                distance = Det(lines[i].direction, lines[i].point - result);
            }
        }

        vec2 ClosestOnSegment(const vec2 &p, const vec2 &a, const vec2 &b)
        {
            const vec2 ab = b - a;
            const float lengthSq = glm::dot(ab, ab);
            return lengthSq < kEpsilon ? a : a + ab * std::clamp(glm::dot(p - a, ab) / lengthSq, 0.0f, 1.0f);
        }

        vec2 Flat(const vec3 &v)
        {
            return vec2(v.x, v.z);
        }
    } // namespace

    struct NavSwarm::Impl
    {
        struct Member
        {
            vec3 pos{0.0f}, previous{0.0f}; // previous: before the last step, for GetInterpolated
            vec2 vel{0.0f}, next{0.0f}, pref{0.0f};
            float radius = 0.3f, speed = 3.5f, stop = 0.0f;
            dtPolyRef ref = 0;
            int crowdAgent = -1;
            bool alive = false, anchored = false, settled = false, yields = false, direct = false;
            bool recheck = true; // new or moved: check `direct` on the next step, not its staggered one

            bool Holds() const { return (anchored && !yields) || settled || speed == 0.0f; }
        };

        const NavMesh::Impl *nav = nullptr;
        const dtMeshTile *tile = nullptr;
        std::vector<Member> members;
        std::vector<int> freeSlots;
        int alive = 0;

        vec3 target{0.0f};
        dtPolyRef targetRef = 0, fieldRef = 0;
        bool hasTarget = false;
        // The flow field, per polygon: the neighbour one step nearer the target (-1 at the target or cut off) and
        // the edge shared with it.
        std::vector<int> towards;
        std::vector<vec2> portalA, portalB, centres;
        // The mesh-boundary edges, by owning polygon (walls[ownStart[p], ownStart[p + 1])), and per polygon those of
        // the connected polygons within wallReach of it: walls[wallIndex[wallStart[p], wallStart[p + 1])].
        struct Wall
        {
            vec2 a{0.0f}, b{0.0f}, out{0.0f}; // out: the normal pointing off the mesh
        };
        std::vector<Wall> walls;
        std::vector<int> ownStart, wallStart, wallIndex;
        float wallReach = 0.0f;

        float carry = 0.0f;
        uint32_t stepCount = 0;
        NavSwarmStats stats;

        std::vector<int> cellHead, cellNext;        // the neighbour grid
        std::vector<std::pair<float, int>> nearby;  // scratch
        std::vector<Line> lines, projected;         // scratch
        std::vector<char> settles;                  // scratch
        std::vector<std::pair<vec2, vec2>> corners; // scratch: wall ends nearest a member, with their wall's normal
        std::vector<int> spans;                     // scratch: walls whose nearest point lies inside them

        int PolyIndex(dtPolyRef ref) const
        {
            if (!ref || !tile)
                return -1;
            unsigned int salt = 0, it = 0, ip = 0;
            nav->mesh->decodePolyId(ref, salt, it, ip);
            return static_cast<int>(ip) < tile->header->polyCount ? static_cast<int>(ip) : -1;
        }

        vec2 Vertex(int index) const { return vec2(tile->verts[index * 3], tile->verts[index * 3 + 2]); }

        static bool IsWall(unsigned short nei) { return nei == 0 || (nei & DT_EXT_LINK); } // no neighbour in this tile

        // Per polygon, a walk over its neighbours whose bounds lie within `reach` of its own; their walls are the ones
        // a member standing in it can meet within a step (connected only, so another floor's edges never count).
        // ponytail: run once and again when a wider member arrives; fine for one solo tile's few thousand polygons.
        void GatherWalls(float reach)
        {
            wallReach = reach;
            const int count = tile->header->polyCount;
            std::vector<vec2> lo(static_cast<size_t>(count), vec2(std::numeric_limits<float>::max())),
                hi(static_cast<size_t>(count), vec2(-std::numeric_limits<float>::max()));
            for (int p = 0; p < count; ++p)
                for (int v = 0; v < tile->polys[p].vertCount; ++v)
                {
                    lo[p] = glm::min(lo[p], Vertex(tile->polys[p].verts[v]));
                    hi[p] = glm::max(hi[p], Vertex(tile->polys[p].verts[v]));
                }
            std::vector<int> seen(static_cast<size_t>(count), -1), queue;
            wallStart.clear();
            wallIndex.clear();
            for (int p = 0; p < count; ++p)
            {
                wallStart.push_back(static_cast<int>(wallIndex.size()));
                queue.assign(1, p);
                seen[p] = p;
                for (size_t q = 0; q < queue.size(); ++q)
                {
                    const int u = queue[q];
                    for (int w = ownStart[u]; w < ownStart[u + 1]; ++w)
                        wallIndex.push_back(w);
                    const dtPoly &poly = tile->polys[u];
                    for (int k = 0; k < poly.vertCount; ++k)
                    {
                        const int v = poly.neis[k] - 1;
                        if (IsWall(poly.neis[k]) || seen[v] == p)
                            continue;
                        const vec2 gap = glm::max(vec2(0.0f), glm::max(lo[v] - hi[p], lo[p] - hi[v]));
                        if (glm::length(gap) <= reach)
                        {
                            seen[v] = p;
                            queue.push_back(v);
                        }
                    }
                }
            }
            wallStart.push_back(static_cast<int>(wallIndex.size()));
        }

        void BuildField()
        {
            const int count = tile->header->polyCount;
            towards.assign(static_cast<size_t>(count), -1);
            portalA.assign(static_cast<size_t>(count), vec2(0.0f));
            portalB.assign(static_cast<size_t>(count), vec2(0.0f));
            fieldRef = targetRef;
            const int start = PolyIndex(targetRef);
            if (start < 0)
                return;
            std::vector<float> cost(static_cast<size_t>(count), std::numeric_limits<float>::max());
            // (cost, polygon): ties settle by polygon index, so the field is deterministic.
            std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<>> open;
            cost[start] = 0.0f;
            open.push({0.0f, start});
            while (!open.empty())
            {
                const auto [c, u] = open.top();
                open.pop();
                if (c > cost[u])
                    continue;
                const dtPoly &poly = tile->polys[u];
                for (unsigned int k = poly.firstLink; k != DT_NULL_LINK; k = tile->links[k].next)
                {
                    const dtLink &link = tile->links[k];
                    const int v = PolyIndex(link.ref);
                    if (v < 0)
                        continue;
                    const float next = c + glm::distance(centres[u], centres[v]);
                    if (next >= cost[v])
                        continue;
                    cost[v] = next;
                    towards[v] = u;
                    portalA[v] = Vertex(poly.verts[link.edge]);
                    portalB[v] = Vertex(poly.verts[(link.edge + 1) % poly.vertCount]);
                    open.push({next, v});
                }
            }
        }

        // Where on polygon `poly`'s exit edge to aim from `p`: the nearest point, kept `radius` from its ends.
        vec2 ExitPoint(int poly, const vec2 &p, float radius) const
        {
            vec2 a = portalA[poly], b = portalB[poly];
            const vec2 ab = b - a;
            const float length = glm::length(ab);
            if (length > 2.0f * radius)
            {
                a += ab / length * radius;
                b -= ab / length * radius;
            }
            else
            {
                a = b = 0.5f * (a + b);
            }
            return ClosestOnSegment(p, a, b);
        }

        void Steer(Member &m, int index)
        {
            m.pref = vec2(0.0f);
            if (!hasTarget)
                return;
            const vec2 p = Flat(m.pos), goal = Flat(target);
            // Straight at the target while the mesh allows it: checked every 4th step, staggered by member.
            if (((stepCount + static_cast<uint32_t>(index)) % 4 == 0 || m.recheck) && targetRef)
            {
                m.recheck = false;
                float t = 0.0f, normal[3];
                dtPolyRef visited[16];
                int visitedCount = 0;
                nav->query->raycast(m.ref, &m.pos.x, &target.x, &nav->filter, &t, normal, visited, &visitedCount, 16);
                // closestPointOnPoly, not getPolyHeight: a target snapped onto a +x/+z edge counts as outside
                float on[3];
                m.direct = t > 1.0f && visitedCount > 0 &&
                           dtStatusSucceed(nav->query->closestPointOnPoly(visited[visitedCount - 1], &target.x, on, nullptr)) &&
                           glm::distance(vec2(on[0], on[2]), goal) < 0.01f &&
                           std::abs(on[1] - target.y) <= std::max(0.1f, nav->settings.cellHeight * 2.0f);
            }
            const bool onTargetFloor = targetRef ? m.direct : std::abs(m.pos.y - target.y) <= nav->settings.agentMaxClimb;
            m.anchored = onTargetFloor && glm::distance(m.pos, target) <= m.stop + (m.anchored ? kAnchorHysteresis : 0.0f);
            if (m.anchored)
                return;
            vec2 aim = goal;
            int poly = m.direct ? -1 : PolyIndex(m.ref);
            for (int hop = 0; hop < 2 && poly >= 0 && towards[poly] >= 0; ++hop)
            {
                aim = ExitPoint(poly, p, m.radius);
                if (glm::distance(aim, p) > kEdgeLookAhead)
                    break;
                poly = towards[poly]; // at this edge already: aim one polygon further
                if (towards[poly] < 0)
                    aim = goal; // that polygon holds the target
            }
            const vec2 dir = aim - p;
            const float length = glm::length(dir);
            if (length < kEpsilon)
                return;
            // Never overshoot the stop line in one step.
            const float remaining = onTargetFloor ? std::max(0.0f, glm::distance(m.pos, target) - m.stop) : length;
            m.pref = dir / length * std::min(m.speed, remaining / kStep);
        }

        void Avoid()
        {
            float maxRadius = 0.0f, maxSpeed = 0.0f;
            vec2 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
            for (const Member &m : members)
                if (m.alive)
                {
                    maxRadius = std::max(maxRadius, m.radius);
                    maxSpeed = std::max(maxSpeed, m.speed);
                    lo = glm::min(lo, Flat(m.pos));
                    hi = glm::max(hi, Flat(m.pos));
                }
            const float range = maxSpeed * kHorizon + 2.0f * maxRadius;
            float cell = std::max(range + maxRadius, 0.5f); // a neighbour counts up to range + its radius
            while ((static_cast<double>(hi.x) - lo.x) / cell + 1 > (1 << 20) ||
                   (static_cast<double>(hi.y) - lo.y) / cell + 1 > (1 << 20) ||
                   ((static_cast<double>(hi.x) - lo.x) / cell + 1) * ((static_cast<double>(hi.y) - lo.y) / cell + 1) > (1 << 20))
                cell *= 2.0f; // a horde spread over kilometres: coarser cells, not a huge grid
            const int width = static_cast<int>((hi.x - lo.x) / cell) + 1, depth = static_cast<int>((hi.y - lo.y) / cell) + 1;
            cellHead.assign(static_cast<size_t>(width) * depth, -1);
            cellNext.assign(members.size(), -1);
            const auto cellOf = [&](const vec2 &p, int &cx, int &cz)
            {
                cx = std::clamp(static_cast<int>((p.x - lo.x) / cell), 0, width - 1);
                cz = std::clamp(static_cast<int>((p.y - lo.y) / cell), 0, depth - 1);
            };
            for (int i = static_cast<int>(members.size()) - 1; i >= 0; --i) // heads end in index order
            {
                if (!members[i].alive)
                    continue;
                int cx = 0, cz = 0;
                cellOf(Flat(members[i].pos), cx, cz);
                int &head = cellHead[static_cast<size_t>(cz) * width + cx];
                cellNext[i] = head;
                head = i;
            }

            // Each neighbour of member i on its floor with their offset and its square, until `visit` returns false.
            const auto scan = [&](size_t i, auto &&visit)
            {
                const vec2 p = Flat(members[i].pos);
                int cx = 0, cz = 0;
                cellOf(p, cx, cz);
                for (int z = std::max(0, cz - 1); z <= std::min(depth - 1, cz + 1); ++z)
                    for (int x = std::max(0, cx - 1); x <= std::min(width - 1, cx + 1); ++x)
                        for (int j = cellHead[static_cast<size_t>(z) * width + x]; j >= 0; j = cellNext[j])
                        {
                            const Member &o = members[j];
                            if (j == static_cast<int>(i) || std::abs(o.pos.y - members[i].pos.y) > kFloorGap)
                                continue;
                            const vec2 d = Flat(o.pos) - p;
                            if (!visit(o, d, glm::dot(d, d)))
                                return;
                        }
            };
            // An anchored member a neighbour overlaps (its true radii: a spawn on top of it, a knockback, a squeeze)
            // moves to part from it, then holds again; one holding still at speed 0 never does.
            // Settle: pressed against a held member that stands in the way and nearer the target, barely moving, and
            // outside every neighbour's avoidance margin (only there does holding still satisfy its own lines; inside,
            // a wedge against walls and held bodies would freeze the overlap). Last step's holds are read (Jacobi), so
            // a held front spreads back one rank per step and lets go the same way.
            settles.assign(members.size(), 0);
            for (size_t i = 0; i < members.size(); ++i)
            {
                Member &m = members[i];
                m.yields = false;
                if (!m.alive || m.crowdAgent >= 0 || m.speed == 0.0f)
                    continue;
                if (m.anchored)
                {
                    scan(i,
                         [&](const Member &o, const vec2 &, float d2)
                         {
                             m.yields = d2 < (m.radius + o.radius) * (m.radius + o.radius);
                             return !m.yields;
                         });
                    continue;
                }
                if (glm::dot(m.pref, m.pref) < kEpsilon || (!m.settled && glm::length(m.vel) > kSettleSpeed * m.speed))
                    continue;
                const float toTarget = glm::distance(Flat(m.pos), Flat(target));
                bool support = false, clear = true;
                scan(i,
                     [&](const Member &o, const vec2 &d, float d2)
                     {
                         const float margin = (m.radius + o.radius) * kAvoidMargin, reach = margin * kSettleReach;
                         clear = d2 >= margin * margin;
                         // Support: swarm members holding for the target, not stopped crowd agents, nor a body standing
                         // on the target (a game's hero): there it walks on to its stop line, which may lie inside reach.
                         const float held = glm::distance(Flat(o.pos), Flat(target));
                         support = support || (o.crowdAgent < 0 && (o.anchored || o.settled) && d2 < reach * reach &&
                                               glm::dot(d, m.pref) > 0.0f && held >= margin && held < toTarget);
                         return clear;
                     });
                settles[i] = support && clear;
            }
            for (size_t i = 0; i < members.size(); ++i)
                members[i].settled = settles[i] != 0;

            const float invHorizon = 1.0f / kHorizon, invStep = 1.0f / kStep;
            for (size_t i = 0; i < members.size(); ++i)
            {
                Member &m = members[i];
                if (!m.alive)
                    continue;
                m.next = vec2(0.0f);
                if (m.Holds())
                    continue; // holds still; the others take the whole avoidance
                const vec2 p = Flat(m.pos);
                nearby.clear();
                int cx = 0, cz = 0;
                cellOf(p, cx, cz);
                for (int z = std::max(0, cz - 1); z <= std::min(depth - 1, cz + 1); ++z)
                    for (int x = std::max(0, cx - 1); x <= std::min(width - 1, cx + 1); ++x)
                        for (int j = cellHead[static_cast<size_t>(z) * width + x]; j >= 0; j = cellNext[j])
                        {
                            if (j == static_cast<int>(i) || std::abs(members[j].pos.y - m.pos.y) > kFloorGap)
                                continue;
                            const vec2 d = Flat(members[j].pos) - p;
                            const float reach = range + members[j].radius;
                            const float d2 = glm::dot(d, d);
                            if (d2 < reach * reach)
                                nearby.emplace_back(d2, j);
                        }
                if (nearby.size() > kMaxNeighbours)
                {
                    std::partial_sort(nearby.begin(), nearby.begin() + kMaxNeighbours, nearby.end());
                    nearby.resize(kMaxNeighbours);
                }
                lines.clear();
                // The surface's clamp as hard lines, first: never into a wall faster than this step reaches it, so a
                // neighbour can count on this member taking its half of their avoidance.
                // A member wider than the bake keeps the difference off the edges (never pushed: inside it, it only
                // may not get closer).
                const float extra = std::max(0.0f, m.radius - nav->settings.agentRadius);
                if (const int poly = PolyIndex(m.ref); poly >= 0)
                {
                    const float reach = m.speed * kStep + extra;
                    const auto bound = [&](const vec2 &c, float d, const vec2 &out)
                    {
                        const vec2 n = d > 1e-4f ? (c - p) / d : out;
                        lines.push_back({n * (std::max(d - extra, 0.0f) * invStep), vec2(-n.y, n.x)});
                    };
                    // A wall whose nearest point lies inside it bounds the velocity along its normal. An end counts
                    // as a corner only when no wall meeting there has its nearest point inside (an obstacle's corner);
                    // where a straight edge just goes on in the next segment, the end would block sliding along it.
                    // Only walls within this step's reach count: one that covers a near corner is nearer still.
                    corners.clear();
                    spans.clear();
                    for (int w = wallStart[poly]; w < wallStart[poly + 1]; ++w)
                    {
                        const Wall &wall = walls[wallIndex[w]];
                        const vec2 ab = wall.b - wall.a;
                        const float t = glm::dot(p - wall.a, ab) / glm::dot(ab, ab);
                        const vec2 c = t <= 0.0f ? wall.a : t >= 1.0f ? wall.b
                                                                      : wall.a + ab * t; // ends exact: matched below
                        const float d = glm::distance(c, p);
                        if (d >= reach)
                            continue;
                        if (t <= 0.0f || t >= 1.0f)
                        {
                            corners.emplace_back(c, wall.out);
                            continue;
                        }
                        spans.push_back(wallIndex[w]);
                        bound(c, d, wall.out);
                    }
                    for (size_t k = 0; k < corners.size(); ++k)
                    {
                        const vec2 c = corners[k].first;
                        bool covered = false;
                        for (size_t q = 0; q < spans.size() && !covered; ++q)
                            covered = walls[spans[q]].a == c || walls[spans[q]].b == c;
                        for (size_t e = 0; e < k && !covered; ++e)
                            covered = corners[e].first == c; // a corner bounds once
                        if (!covered)
                            bound(c, glm::distance(c, p), corners[k].second);
                    }
                }
                const size_t wallLines = lines.size();
                for (const auto &[d2, j] : nearby)
                {
                    const Member &o = members[j];
                    const vec2 relPos = Flat(o.pos) - p, relVel = m.vel - o.vel;
                    const float combined = (m.radius + o.radius) * kAvoidMargin, combinedSq = combined * combined;
                    Line line;
                    vec2 u(0.0f);
                    if (d2 > combinedSq)
                    {
                        const vec2 w = relVel - invHorizon * relPos;
                        const float wLengthSq = glm::dot(w, w), dot1 = glm::dot(w, relPos);
                        if (dot1 < 0.0f && dot1 * dot1 > combinedSq * wLengthSq)
                        {
                            // Off the cut-off circle.
                            const float wLength = std::sqrt(wLengthSq);
                            const vec2 unitW = w / wLength;
                            line.direction = vec2(unitW.y, -unitW.x);
                            u = (combined * invHorizon - wLength) * unitW;
                        }
                        else
                        {
                            // Off a leg of the cone.
                            const float leg = std::sqrt(d2 - combinedSq);
                            if (Det(relPos, w) > 0.0f)
                                line.direction = vec2(relPos.x * leg - relPos.y * combined,
                                                      relPos.x * combined + relPos.y * leg) /
                                                 d2;
                            else
                                line.direction = -vec2(relPos.x * leg + relPos.y * combined,
                                                       -relPos.x * combined + relPos.y * leg) /
                                                 d2;
                            u = glm::dot(relVel, line.direction) * line.direction - relVel;
                        }
                    }
                    else
                    {
                        // Already touching (a spawn on top of another): part within this step.
                        vec2 w = relVel - invStep * relPos;
                        float wLength = glm::length(w);
                        if (wLength < kEpsilon) // same place, same velocity: split along a direction hashed from the pair
                        {
                            const uint32_t lo = static_cast<uint32_t>(std::min(static_cast<int>(i), j)),
                                           hi = static_cast<uint32_t>(std::max(static_cast<int>(i), j));
                            uint32_t h = (lo * 73856093u) ^ (hi * 19349663u);
                            h ^= h >> 13;
                            h *= 0x5bd1e995u;
                            h ^= h >> 15;
                            w = glm::normalize(vec2(static_cast<float>(h & 0xffffu) - 32767.5f, static_cast<float>(h >> 16) - 32767.5f)) *
                                (static_cast<uint32_t>(i) == lo ? -1.0f : 1.0f);
                            wLength = 1.0f;
                        }
                        const vec2 unitW = w / wLength;
                        line.direction = vec2(unitW.y, -unitW.x);
                        u = (combined * invStep - wLength) * unitW;
                    }
                    // Reciprocal: each takes half; a member holding still takes none, so this one takes it all.
                    line.point = m.vel + (o.Holds() ? 1.0f : 0.5f) * u;
                    lines.push_back(line);
                }
                vec2 result(0.0f);
                vec2 optimal = m.pref;
                if (glm::dot(optimal, optimal) > kEpsilon)
                    for (const auto &[d2, j] : nearby)
                    {
                        const Member &o = members[j];
                        const vec2 delta = Flat(o.pos) - p;
                        const float radius = (m.radius + o.radius) * kAvoidMargin;
                        // Never round a body standing on the target (a game's hero): passing it gets no nearer, and
                        // the member would slide round it at full speed instead of stopping at its stop line.
                        if (o.Holds() && d2 > kEpsilon && glm::dot(optimal, delta) > 0.0f &&
                            std::abs(Det(glm::normalize(optimal), delta)) < radius &&
                            glm::distance(Flat(o.pos), Flat(target)) >= radius)
                        {
                            // ponytail: local tangential preference for held bodies; replan around dynamic obstacles
                            // if a crowd blocks an entire passage. ORCA alone can choose zero dead ahead. Pass on the
                            // side this member is already offset to: one fixed hand swirls a whole pack.
                            const vec2 side = vec2(-delta.y, delta.x) / std::sqrt(d2) * (Det(optimal, delta) > 0.0f ? -1.0f : 1.0f);
                            optimal = glm::normalize(optimal + side * m.speed) * m.speed;
                            break;
                        }
                    }
                const size_t failed = LinearProgram2(lines, m.speed, optimal, false, result);
                if (failed < lines.size())
                    LinearProgram3(lines, wallLines, failed, m.speed, result, projected);
                m.next = result;
            }
        }

        void Move(Member &m)
        {
            if (!m.alive)
                return;
            const vec3 dest = m.pos + vec3(m.next.x, 0.0f, m.next.y) * kStep;
            float result[3];
            dtPolyRef visited[16];
            int visitedCount = 0;
            if (dtStatusFailed(nav->query->moveAlongSurface(m.ref, &m.pos.x, &dest.x, &nav->filter, result, visited,
                                                            &visitedCount, 16)) ||
                visitedCount == 0)
            {
                m.vel = vec2(0.0f);
                return;
            }
            m.ref = visited[visitedCount - 1];
            float onPoly[3]; // closestPointOnPoly, not getPolyHeight: a step clamped onto a +x/+z edge counts as outside
            if (dtStatusFailed(nav->query->closestPointOnPoly(m.ref, result, onPoly, nullptr)))
                onPoly[1] = result[1];
            const vec3 moved(result[0], onPoly[1], result[2]);
            m.vel = (Flat(moved) - Flat(m.pos)) * (1.0f / kStep); // what the surface allowed
            m.pos = moved;
        }

        void Step(NavCrowd::Impl *crowd)
        {
            if (hasTarget && (targetRef != fieldRef || towards.empty()))
                BuildField();
            const size_t swarmSize = members.size();
            for (size_t i = 0; i < swarmSize; ++i)
                if (members[i].alive)
                {
                    members[i].previous = members[i].pos;
                    Steer(members[i], static_cast<int>(i));
                }
            if (crowd && crowd->crowd)
            {
                for (int i = 0; i < NavCrowd::kMaxAgents; ++i)
                {
                    const dtCrowdAgent *a = crowd->crowd->getAgent(i);
                    if (!a->active)
                        continue;
                    Member m;
                    m.pos = vec3(a->npos[0], a->npos[1], a->npos[2]);
                    crowd->previous[i] = m.pos;
                    m.vel = vec2(a->vel[0], a->vel[2]);
                    m.radius = a->params.radius;
                    m.speed = a->params.maxSpeed;
                    m.ref = a->corridor.getFirstPoly();
                    m.alive = true;
                    m.anchored = a->targetState == DT_CROWDAGENT_TARGET_NONE;
                    m.crowdAgent = i;
                    members.push_back(m);
                }
                crowd->crowd->update(kStep, nullptr);
                for (size_t i = swarmSize; i < members.size(); ++i)
                {
                    const dtCrowdAgent *a = crowd->crowd->getAgent(members[i].crowdAgent);
                    members[i].pref = vec2(a->vel[0], a->vel[2]);
                }
            }
            Avoid();
            for (Member &m : members)
                Move(m);
            for (size_t i = swarmSize; i < members.size(); ++i)
            {
                const Member &m = members[i];
                dtCrowdAgent *a = crowd->crowd->getEditableAgent(m.crowdAgent);
                a->corridor.movePosition(&m.pos.x, nav->query, &nav->filter);
                const float *p = a->corridor.getPos();
                std::copy(p, p + 3, a->npos);
                a->vel[0] = m.vel.x;
                a->vel[1] = 0.0f;
                a->vel[2] = m.vel.y;
                a->boundary.reset();
            }
            members.resize(swarmSize);
            ++stepCount;
        }
    };

    NavSwarm::NavSwarm(const NavMesh &mesh) : m_impl(std::make_unique<Impl>())
    {
        Impl &s = *m_impl;
        s.nav = mesh.m_impl.get();
        if (!s.nav->mesh)
            return;
        const dtNavMesh *nav = s.nav->mesh;
        for (int i = 0; i < nav->getMaxTiles() && !s.tile; ++i)
        {
            const dtMeshTile *tile = nav->getTile(i);
            if (tile && tile->header)
                s.tile = tile;
        }
        if (!s.tile)
            return;
        s.centres.resize(static_cast<size_t>(s.tile->header->polyCount));
        for (int p = 0; p < s.tile->header->polyCount; ++p)
        {
            const dtPoly &poly = s.tile->polys[p];
            vec2 sum(0.0f);
            for (int v = 0; v < poly.vertCount; ++v)
                sum += s.Vertex(poly.verts[v]);
            s.centres[p] = sum / static_cast<float>(std::max<int>(1, poly.vertCount));
        }
        for (int p = 0; p < s.tile->header->polyCount; ++p)
        {
            s.ownStart.push_back(static_cast<int>(s.walls.size()));
            const dtPoly &poly = s.tile->polys[p];
            for (int k = 0; k < poly.vertCount; ++k)
            {
                const vec2 a = s.Vertex(poly.verts[k]), b = s.Vertex(poly.verts[(k + 1) % poly.vertCount]);
                const float length = glm::distance(a, b);
                if (!Impl::IsWall(poly.neis[k]) || length < kEpsilon)
                    continue;
                vec2 out = vec2(b.y - a.y, a.x - b.x) / length;
                if (glm::dot(out, 0.5f * (a + b) - s.centres[p]) < 0.0f)
                    out = -out;
                s.walls.push_back({a, b, out});
            }
        }
        s.ownStart.push_back(static_cast<int>(s.walls.size()));
        s.GatherWalls(kWallReach);
    }

    NavSwarm::~NavSwarm() = default;

    int NavSwarm::Add(const vec3 &position, const NavSwarmMember &member)
    {
        Impl &s = *m_impl;
        if (!std::isfinite(member.radius) || member.radius <= 0.0f || member.radius > kNavMaxCoordinate ||
            !std::isfinite(member.speed) || member.speed < 0.0f || member.speed > kNavMaxSpeed ||
            !std::isfinite(member.stopDistance) || member.stopDistance < 0.0f || member.stopDistance > kNavMaxCoordinate)
        {
            PE_WARN("[Nav] invalid swarm radius/speed/stop distance");
            return -1;
        }
        dtPolyRef ref = 0;
        vec3 start;
        if (!s.tile || !s.nav->Snap(position, ref, &start.x))
            return -1;
        if (const float reach = kWallReach + member.radius - s.nav->settings.agentRadius; reach > s.wallReach)
            s.GatherWalls(reach);
        int index = static_cast<int>(s.members.size());
        if (!s.freeSlots.empty())
        {
            index = s.freeSlots.back();
            s.freeSlots.pop_back();
        }
        else
        {
            s.members.emplace_back();
        }
        Impl::Member &m = s.members[index];
        m = Impl::Member{};
        m.pos = m.previous = start;
        m.radius = member.radius;
        m.speed = member.speed;
        m.stop = member.stopDistance;
        m.ref = ref;
        m.alive = true;
        ++s.alive;
        return index;
    }

    bool NavSwarm::SetSpeed(int member, float speed)
    {
        Impl &s = *m_impl;
        if (member < 0 || member >= static_cast<int>(s.members.size()) || !s.members[member].alive ||
            !std::isfinite(speed) || speed < 0.0f || speed > kNavMaxSpeed)
            return false;
        s.members[member].speed = speed;
        return true;
    }

    bool NavSwarm::SetPosition(int member, const vec3 &position)
    {
        Impl &s = *m_impl;
        dtPolyRef ref = 0;
        vec3 snapped;
        if (member < 0 || member >= static_cast<int>(s.members.size()) || !s.members[member].alive ||
            !s.nav->Snap(position, ref, &snapped.x))
            return false;
        Impl::Member &m = s.members[member];
        m.previous += snapped - m.pos; // drawn shifted by the same offset, not smeared across it
        m.recheck = true;
        m.pos = snapped;
        m.ref = ref;
        return true;
    }

    void NavSwarm::Remove(int member)
    {
        Impl &s = *m_impl;
        if (member < 0 || member >= static_cast<int>(s.members.size()) || !s.members[member].alive)
            return;
        s.members[member].alive = false;
        s.freeSlots.push_back(member);
        --s.alive;
    }

    void NavSwarm::SetTarget(const vec3 &target)
    {
        Impl &s = *m_impl;
        if (!ValidNavPoint(target))
            return;
        s.target = target;
        s.hasTarget = true;
        vec3 snapped;
        if (!s.tile || !s.nav->Snap(target, s.targetRef, &snapped.x))
            s.targetRef = 0; // off the mesh: members head straight for it and slide along what stops them
        else
            s.target = snapped;
        if (s.targetRef != s.fieldRef)
            for (Impl::Member &m : s.members)
                m.direct = false;
    }

    int NavSwarm::Update(float dt, NavCrowd *crowd)
    {
        Impl &s = *m_impl;
        s.stats.members = s.alive;
        s.stats.steps = 0;
        if (!s.tile || !std::isfinite(dt) || dt <= 0.0f)
            return 0;
        NavCrowd::Impl *c = crowd ? crowd->m_impl.get() : nullptr;
        if (c && c->nav != s.nav)
            return 0;
        bool hasCrowd = false;
        if (c && c->crowd)
            for (int i = 0; i < NavCrowd::kMaxAgents && !hasCrowd; ++i)
                hasCrowd = c->crowd->getAgent(i)->active;
        if (!s.alive && !hasCrowd)
        {
            s.carry = 0.0f;
            s.stats.stepMs = 0.0f;
            return 0;
        }
        s.carry += std::min(dt, kStep * kNavMaxStepsPerUpdate);
        const auto started = std::chrono::steady_clock::now();
        while (s.carry >= kStep && s.stats.steps < kNavMaxStepsPerUpdate)
        {
            s.Step(c);
            s.carry -= kStep;
            ++s.stats.steps;
        }
        if (s.stats.steps == kNavMaxStepsPerUpdate)
            s.carry = std::min(s.carry, kStep); // a hitch: drop the backlog instead of spiralling
        if (c)
            c->alpha = s.carry / kStep; // the crowd took these steps too
        if (s.stats.steps > 0)
            s.stats.stepMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count() /
                             static_cast<float>(s.stats.steps);
        return s.stats.steps;
    }

    bool NavSwarm::Get(int member, vec3 &position, vec3 &velocity) const
    {
        const Impl &s = *m_impl;
        if (member < 0 || member >= static_cast<int>(s.members.size()) || !s.members[member].alive)
            return false;
        const Impl::Member &m = s.members[member];
        position = m.pos;
        velocity = vec3(m.vel.x, 0.0f, m.vel.y);
        return true;
    }

    bool NavSwarm::GetInterpolated(int member, vec3 &position) const
    {
        const Impl &s = *m_impl;
        if (member < 0 || member >= static_cast<int>(s.members.size()) || !s.members[member].alive)
            return false;
        position = glm::mix(s.members[member].previous, s.members[member].pos, s.carry / kStep);
        return true;
    }

    bool NavSwarm::IsAnchored(int member) const
    {
        const Impl &s = *m_impl;
        return member >= 0 && member < static_cast<int>(s.members.size()) && s.members[member].alive &&
               s.members[member].anchored;
    }

    const NavSwarmStats &NavSwarm::Stats() const
    {
        return m_impl->stats;
    }
#else
    struct NavSwarm::Impl
    {
        NavSwarmStats stats;
    };

    NavSwarm::NavSwarm(const NavMesh &) : m_impl(std::make_unique<Impl>()) {}
    NavSwarm::~NavSwarm() = default;
    int NavSwarm::Add(const vec3 &, const NavSwarmMember &)
    {
        return -1;
    }
    bool NavSwarm::SetSpeed(int, float)
    {
        return false;
    }
    bool NavSwarm::SetPosition(int, const vec3 &)
    {
        return false;
    }
    void NavSwarm::Remove(int) {}
    void NavSwarm::SetTarget(const vec3 &) {}
    int NavSwarm::Update(float, NavCrowd *)
    {
        return 0;
    }
    bool NavSwarm::Get(int, vec3 &, vec3 &) const
    {
        return false;
    }
    bool NavSwarm::GetInterpolated(int, vec3 &) const
    {
        return false;
    }
    bool NavSwarm::IsAnchored(int) const
    {
        return false;
    }
    const NavSwarmStats &NavSwarm::Stats() const
    {
        return m_impl->stats;
    }
#endif
} // namespace pe
