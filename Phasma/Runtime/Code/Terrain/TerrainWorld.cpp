#include "Terrain/TerrainWorld.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Voxel/ITerrainGenerator.h"
#include "Voxel/VoxelWorld.h" // voxel::VoxelConfig (full definition, for the generator seam)
#include "Voxel/MapGen.h"     // reuse the shared worldgen seam (heightmap SurfaceHeight)
#include "Voxel/NoiseGen.h"   // reuse the shared worldgen seam (noise SurfaceHeight)
#include "Voxel/VoxelTypes.h" // kSectionDim (blocks per column)
#ifdef PE_PHYSICS
#include "ECS/Context.h"           // GetGlobalSystem
#include "Systems/PhysicsSystem.h" // static triangle-mesh terrain collider
#endif
#include <meshoptimizer.h> // meshopt_simplify: standard discrete LODs on the terrain mesh

namespace pe::terrain
{
    namespace
    {
        constexpr int kBlocksPerColumn = voxel::kSectionDim; // 16
        constexpr int kTileCells = 64;                       // mesh tile size in X/Z blocks
        constexpr int kMaxGridPerSide = 2048;                // cap the built grid so a huge radius can't OOM the mesh

        // A single heightfield tile: standard float vertices + indices, rendered through the normal mesh path.
        struct TileMesh
        {
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            vec3 aabbMin = vec3(0.0f);
            vec3 aabbMax = vec3(0.0f);
        };

        // MapGen/NoiseGen read a voxel::VoxelConfig; fill only the fields SurfaceHeight / Valid /
        // WorldRadiusColumns actually use. ponytail: the generator seam still lives in voxel::; a shared
        // Worldgen config is the cleaner home if a third consumer ever appears.
        voxel::VoxelConfig ToVoxelConfig(const TerrainConfig &cfg)
        {
            voxel::VoxelConfig vc{};
            vc.heightmapPath = cfg.heightmapPath;
            vc.blocksPerPixel = std::max(1, (int)std::lround(cfg.metersPerPixel));
            vc.surfaceMetersPerPixel = std::max(0.05f, cfg.metersPerPixel); // float extent for SurfaceHeight
            vc.groundHeight = cfg.groundHeight;
            vc.heightMin = cfg.heightMin;
            vc.heightMax = cfg.heightMax;
            vc.boundsCenterCx = cfg.boundsCenterCx;
            vc.boundsCenterCz = cfg.boundsCenterCz;
            vc.streaming = false;
            return vc;
        }
    } // namespace

    TerrainWorld::TerrainWorld() = default;
    TerrainWorld::~TerrainWorld()
    {
        Destroy();
    }

    void TerrainWorld::SetTerrainGenerator(std::shared_ptr<voxel::ITerrainGenerator> generator)
    {
        m_generator = std::move(generator);
        m_generatorOverridden = (m_generator != nullptr);
    }

    void TerrainWorld::Create(Scene *scene, const TerrainConfig &cfg)
    {
        m_scene = scene;
        m_cfg = cfg;
        m_cfg.sizeXMeters = std::max(0, m_cfg.sizeXMeters);
        m_cfg.sizeZMeters = std::max(0, m_cfg.sizeZMeters);

        // Engine ships default generators; a game keeps its own via SetTerrainGenerator. A configured
        // heightmap selects MapGen (a failed load falls back to noise); both expose SurfaceHeight(x,z).
        if (!m_generatorOverridden)
        {
            m_generator.reset();
            if (!m_cfg.heightmapPath.empty())
            {
                voxel::VoxelConfig vc = ToVoxelConfig(m_cfg);
                auto mapGen = std::make_shared<voxel::MapGen>(vc);
                if (mapGen->Valid())
                {
                    // 0 on an axis: fill exactly the map's extent on that axis.
                    if (m_cfg.sizeXMeters == 0)
                        m_cfg.sizeXMeters = mapGen->MapBlocksX();
                    if (m_cfg.sizeZMeters == 0)
                        m_cfg.sizeZMeters = mapGen->MapBlocksZ();
                    m_generator = std::move(mapGen);
                }
            }
            if (!m_generator)
            {
                voxel::NoiseParams p{};
                p.groundY = 64;
                p.amplitude = 28.0f; // ponytail: fixed domain-warp strength; surface peak height is the Height Range
                p.groundHeight = m_cfg.groundHeight;
                p.heightMin = m_cfg.heightMin;
                p.heightMax = m_cfg.heightMax;
                p.featureScale = m_cfg.noiseFeatureScale;
                p.seed = m_cfg.noiseSeed;
                p.caves = false; // surface only
                p.seaLevel = -1;
                m_generator = std::make_shared<voxel::NoiseGen>(p);
            }
        }
        if (m_cfg.sizeXMeters <= 0)
            m_cfg.sizeXMeters = 256; // no map + no explicit size: a default patch
        if (m_cfg.sizeZMeters <= 0)
            m_cfg.sizeZMeters = 256;

        BuildField();
    }

    float TerrainWorld::FieldAt(int i, int j) const
    {
        i = std::clamp(i, 0, m_nx);
        j = std::clamp(j, 0, m_nz);
        return m_height[static_cast<size_t>(i) + static_cast<size_t>(m_nx + 1) * j];
    }

    void TerrainWorld::BuildField()
    {
        // One grid cell spans `step` metres, so the mesh has size/step cells per axis: a coarse map (big
        // metersPerPixel) covers a large world with a bounded vertex count. Cells are capped, not metres.
        m_step = std::max(0.05f, m_cfg.metersPerPixel);
        const int sx = std::max(2, m_cfg.sizeXMeters);
        const int sz = std::max(2, m_cfg.sizeZMeters);
        int nx = std::max(2, (int)std::lround(sx / m_step));
        int nz = std::max(2, (int)std::lround(sz / m_step));
        if (nx > kMaxGridPerSide || nz > kMaxGridPerSide)
        {
            PE_WARN("Terrain: %dx%d cells exceeds the %d-cell mesh cap; clamped (raise Meters/Pixel for a "
                    "bigger world).",
                    nx, nz, kMaxGridPerSide);
            nx = std::min(nx, kMaxGridPerSide);
            nz = std::min(nz, kMaxGridPerSide);
        }
        m_nx = nx;
        m_nz = nz;
        const float halfWX = nx * m_step * 0.5f, halfWZ = nz * m_step * 0.5f;
        const int centerX = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2;
        const int centerZ = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2;
        m_origin = vec3(centerX - halfWX, 0.0f, centerZ - halfWZ);

        const int cnx = m_nx + 1, cnz = m_nz + 1;
        m_height.assign(static_cast<size_t>(cnx) * cnz, m_cfg.groundHeight);
        if (voxel::ITerrainGenerator *gen = m_generator.get())
            for (int j = 0; j < cnz; ++j)
                for (int i = 0; i < cnx; ++i)
                    m_height[static_cast<size_t>(i) + static_cast<size_t>(cnx) * j] =
                        gen->SurfaceHeight(m_origin.x + i * m_step, m_origin.z + j * m_step);

        RebuildMesh();
    }

    void TerrainWorld::RebuildMesh()
    {
        if (!m_scene || m_height.empty())
            return;

        const int nx = m_nx, nz = m_nz;

        // Split the height grid into tiles, each an independent standard mesh so it frustum/Hi-Z culls and
        // LODs on its own. Neighbour tiles share their boundary vertex column (identical heights from the
        // one field) so seams are watertight with no apron.
        std::vector<TileMesh> tiles;
        float gYmin = 1e30f, gYmax = -1e30f;
        for (int tz = 0; tz < nz; tz += kTileCells)
        {
            for (int tx = 0; tx < nx; tx += kTileCells)
            {
                const int bcx = std::min(kTileCells, nx - tx);
                const int bcz = std::min(kTileCells, nz - tz);
                const int cx = bcx + 1, cz = bcz + 1; // corner counts (inclusive of the shared edge)

                TileMesh tile;
                tile.vertices.reserve(static_cast<size_t>(cx) * cz);
                float yMin = 1e30f, yMax = -1e30f;
                for (int j = 0; j < cz; ++j)
                    for (int i = 0; i < cx; ++i)
                    {
                        const int gi = tx + i, gj = tz + j;
                        const float wy = FieldAt(gi, gj);
                        yMin = std::min(yMin, wy);
                        yMax = std::max(yMax, wy);
                        // Normal = -gradient of the surface: dh/dx ~ (hR-hL)/2, dh/dz ~ (hU-hD)/2.
                        const float hL = FieldAt(gi - 1, gj), hR = FieldAt(gi + 1, gj);
                        const float hD = FieldAt(gi, gj - 1), hU = FieldAt(gi, gj + 1);
                        // Neighbours are m_step metres apart, so the gradient denominator scales with it.
                        const vec3 n = glm::normalize(vec3(hL - hR, 2.0f * m_step, hD - hU));
                        Vertex v{};
                        v.position[0] = m_origin.x + gi * m_step;
                        v.position[1] = wy;
                        v.position[2] = m_origin.z + gj * m_step;
                        v.normals[0] = n.x;
                        v.normals[1] = n.y;
                        v.normals[2] = n.z;
                        v.uv[0] = static_cast<float>(gi);
                        v.uv[1] = static_cast<float>(gj);
                        tile.vertices.push_back(v);
                    }
                tile.indices.reserve(static_cast<size_t>(bcx) * bcz * 6);
                for (int j = 0; j < bcz; ++j)
                    for (int i = 0; i < bcx; ++i)
                    {
                        const uint32_t a = static_cast<uint32_t>(j * cx + i);
                        const uint32_t b = a + 1;
                        const uint32_t c = a + cx;
                        const uint32_t d = c + 1;
                        // CCW seen from above (engine FrontFace=CW + cull FRONT keeps CCW-from-outside).
                        tile.indices.push_back(a);
                        tile.indices.push_back(c);
                        tile.indices.push_back(b);
                        tile.indices.push_back(b);
                        tile.indices.push_back(c);
                        tile.indices.push_back(d);
                    }
                if (tile.vertices.empty())
                    continue;
                tile.aabbMin = vec3(m_origin.x + tx * m_step, yMin, m_origin.z + tz * m_step);
                tile.aabbMax = vec3(m_origin.x + (tx + bcx) * m_step, yMax, m_origin.z + (tz + bcz) * m_step);
                gYmin = std::min(gYmin, yMin);
                gYmax = std::max(gYmax, yMax);
                tiles.push_back(std::move(tile));
            }
        }
        if (tiles.empty())
            return;

        // Tear down previous tile meshes/node — a rebuild replaces the whole set. ponytail: the old verts
        // stay in the shared store until the next full scene rebuild (a per-rebuild leak, as voxel smooth).
        for (int idx : m_meshIndices)
            if (idx >= 0 && m_scene->IsValidMeshIndex(idx))
                m_scene->GetMesh(idx).material = nullptr;
        m_meshIndices.clear();
        if (m_hostNode && m_scene->IsNodeAlive(m_hostNode))
            m_scene->DeleteNode(m_hostNode);
        while (NodeId *stale = m_scene->FindNodeByName("TerrainHost"))
            m_scene->DeleteNode(stale);
        m_hostNode = nullptr;

        m_material = std::make_unique<Material>();
        m_material->name = "TerrainMaterial";
        m_material->baseColorFactor = vec4(1.0f); // white — per-vertex colour carries the terrain bands
        m_material->metallic = 0.0f;
        m_material->roughness = 1.0f;
        m_material->occlusionStrength = 1.0f;
        m_material->textureMask = 0u;
        m_material->renderType = RenderType::Opaque;
        m_material->SyncParamsFromLegacy();

        // Colour bands normalize to the GLOBAL Y range so the sand/grass/rock/snow cutoffs don't jump at
        // tile borders. GBufferPS multiplies albedo * input.color, so this needs no shader change.
        // ponytail: fixed bands; triplanar atlas later.
        const float ymin = gYmin;
        const float yspan = std::max(1.0f, gYmax - gYmin);
        const float seaY = m_cfg.seaLevelM;
        const auto terrainColor = [&](const Vertex &v) -> vec3
        {
            const float f = (v.position[1] - ymin) / yspan;
            vec3 c = f < 0.08f   ? vec3(0.76f, 0.70f, 0.50f)
                     : f < 0.58f ? vec3(0.30f, 0.52f, 0.24f)
                     : f < 0.90f ? vec3(0.45f, 0.42f, 0.38f)
                                 : vec3(0.92f, 0.94f, 0.96f);
            if (v.normals[1] < 0.5f && f < 0.90f)
                c = vec3(0.42f, 0.39f, 0.36f);
            if (v.position[1] < seaY)
                c = glm::mix(c, vec3(0.16f, 0.26f, 0.40f), 0.6f);
            return c;
        };

        std::vector<Vertex> &vertices = m_scene->GetVertexStore();
        std::vector<PositionUvVertex> &positionUvs = m_scene->GetPositionUvStore();
        std::vector<AabbVertex> &aabbVertices = m_scene->GetAabbVertexStore();
        std::vector<uint32_t> &indices = m_scene->GetIndexStore();

        m_hostNode = m_scene->CreateNode("TerrainHost");
        m_scene->SetLocalMatrix(m_hostNode, mat4(1.0f), false);

        size_t totalVerts = 0, totalTris = 0;
        for (TileMesh &tile : tiles)
        {
            const uint32_t vertexBase = static_cast<uint32_t>(vertices.size());
            const uint32_t positionBase = static_cast<uint32_t>(positionUvs.size());
            const size_t aabbBase = aabbVertices.size();
            const uint32_t indexBase = static_cast<uint32_t>(indices.size());

            // Depth-prepass/shadows bind PositionUvVertex at the same vertexOffset as the GBuffer Vertex
            // stream, so every vertex needs a matching entry in both.
            for (const Vertex &src : tile.vertices)
            {
                Vertex v = src;
                const vec3 c = terrainColor(v);
                v.color[0] = c.x;
                v.color[1] = c.y;
                v.color[2] = c.z;
                v.color[3] = 1.0f;
                vertices.push_back(v);
                PositionUvVertex pv{};
                pv.position[0] = v.position[0];
                pv.position[1] = v.position[1];
                pv.position[2] = v.position[2];
                pv.uv[0] = v.uv[0];
                pv.uv[1] = v.uv[1];
                positionUvs.push_back(pv);
            }
            for (int c = 0; c < 8; ++c)
            {
                AabbVertex av{};
                av.position[0] = (c & 1) ? tile.aabbMax.x : tile.aabbMin.x;
                av.position[1] = (c & 2) ? tile.aabbMax.y : tile.aabbMin.y;
                av.position[2] = (c & 4) ? tile.aabbMax.z : tile.aabbMin.z;
                aabbVertices.push_back(av);
            }
            for (uint32_t idx : tile.indices)
                indices.push_back(idx);

            Mesh m{};
            m.vertexOffset = vertexBase;
            m.vertexCount = static_cast<uint32_t>(tile.vertices.size());
            m.indexOffset = indexBase;
            m.indexCount = static_cast<uint32_t>(tile.indices.size());
            m.positionsOffset = positionBase;
            m.aabbVertexOffset = aabbBase;
            m.aabbColor = 0xFFFFFFFF;
            m.boundingBox = {tile.aabbMin, tile.aabbMax};
            m.renderType = RenderType::Opaque;
            m.material = m_material.get();

            // Standard mesh LOD via meshopt (border-locked so tile seams stay matched at every level):
            // reduced index sets over the SAME verts, appended to the shared store; GPU CullingCS
            // distance-picks the level, exactly like model meshes.
            m.lodIndexOffset[0] = m.indexOffset;
            m.lodIndexCount[0] = m.indexCount;
            m.lodCount = 1;
            if (m.indexCount >= 256)
            {
                const float *positions = reinterpret_cast<const float *>(tile.vertices.data());
                const size_t vtxCount = tile.vertices.size();
                static constexpr float kRatios[Mesh::kMaxLods] = {1.0f, 0.5f, 0.25f, 0.12f};
                std::vector<uint32_t> simplified(tile.indices.size());
                uint32_t prevCount = m.indexCount;
                for (uint32_t lod = 1; lod < Mesh::kMaxLods; ++lod)
                {
                    const size_t target = (static_cast<size_t>(m.indexCount * kRatios[lod]) / 3) * 3;
                    if (target < 12)
                        break;
                    float err = 0.0f;
                    const size_t resCount =
                        meshopt_simplify(simplified.data(), tile.indices.data(), tile.indices.size(), positions,
                                         vtxCount, sizeof(Vertex), target, 0.1f, meshopt_SimplifyLockBorder, &err);
                    if (resCount == 0 || resCount >= static_cast<size_t>(prevCount * 0.95f))
                        break;
                    const uint32_t off = static_cast<uint32_t>(indices.size());
                    indices.insert(indices.end(), simplified.begin(), simplified.begin() + resCount);
                    m.lodIndexOffset[lod] = off;
                    m.lodIndexCount[lod] = static_cast<uint32_t>(resCount);
                    m.lodCount = lod + 1;
                    prevCount = static_cast<uint32_t>(resCount);
                }
            }

            totalVerts += tile.vertices.size();
            totalTris += tile.indices.size() / 3;
            const int meshIdx = m_scene->AddMesh(std::move(m));
            m_meshIndices.push_back(meshIdx);
            m_scene->AddMeshRef(m_hostNode, meshIdx);
        }
        PE_INFO("Terrain: %zu tiles, %zu verts / %zu tris over %dx%d cells", m_meshIndices.size(), totalVerts,
                totalTris, nx, nz);
        // ponytail: a runtime AddMesh here would clobber a coexisting cube-voxel GeometryArena reservation;
        // fine while terrain and cube worlds live in separate scenes. Build terrain before reserving the
        // arena if they ever share one.
        m_scene->SetGeometryDirty();
        m_scene->FlushPendingGpuWork();
        UpdateCollider();
    }

    void TerrainWorld::Update()
    {
        if (m_pendingSculpts.empty())
            return;
        std::vector<PendingSculpt> sculpts;
        sculpts.swap(m_pendingSculpts);
        for (const PendingSculpt &s : sculpts)
            Sculpt(s.center, s.radius, s.amount);
    }

    void TerrainWorld::Sculpt(const vec3 &center, float radius, float amount)
    {
        if (m_height.empty() || radius <= 0.0f)
            return;
        const int cnx = m_nx + 1, cnz = m_nz + 1;
        const int i0 = std::max(0, (int)std::floor((center.x - radius - m_origin.x) / m_step));
        const int i1 = std::min(cnx - 1, (int)std::ceil((center.x + radius - m_origin.x) / m_step));
        const int j0 = std::max(0, (int)std::floor((center.z - radius - m_origin.z) / m_step));
        const int j1 = std::min(cnz - 1, (int)std::ceil((center.z + radius - m_origin.z) / m_step));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
            {
                const float dx = (m_origin.x + i * m_step) - center.x;
                const float dz = (m_origin.z + j * m_step) - center.z;
                const float dist = std::sqrt(dx * dx + dz * dz);
                if (dist > radius)
                    continue;
                m_height[static_cast<size_t>(i) + static_cast<size_t>(cnx) * j] += amount * (1.0f - dist / radius);
            }
        RebuildMesh();
    }

    void TerrainWorld::QueueSculpt(const vec3 &center, float radius, float amount)
    {
        if (radius > 0.0f)
            m_pendingSculpts.push_back({center, radius, amount});
    }

    float TerrainWorld::SampleHeight(float x, float z) const
    {
        if (m_height.empty())
            return m_cfg.groundHeight + m_cfg.heightMin;
        const int cnx = m_nx + 1;
        const float gx = std::clamp((x - m_origin.x) / m_step, 0.0f, (float)m_nx);
        const float gz = std::clamp((z - m_origin.z) / m_step, 0.0f, (float)m_nz);
        const int x0 = (int)gx, z0 = (int)gz;
        const int x1 = std::min(x0 + 1, m_nx), z1 = std::min(z0 + 1, m_nz);
        const float tx = gx - x0, tz = gz - z0;
        auto H = [&](int i, int j)
        { return m_height[static_cast<size_t>(i) + static_cast<size_t>(cnx) * j]; };
        const float a = H(x0, z0) * (1 - tx) + H(x1, z0) * tx;
        const float b = H(x0, z1) * (1 - tx) + H(x1, z1) * tx;
        return a * (1 - tz) + b * tz;
    }

    bool TerrainWorld::Raycast(const vec3 &o, const vec3 &d, float maxDist, vec3 &hitPoint, vec3 &hitNormal) const
    {
        if (m_height.empty() || glm::length(d) < 1e-6f)
            return false;
        const vec3 dir = glm::normalize(d);
        // ponytail: fixed 0.5-block march + linear crossing refine. Fine for terrain rays.
        constexpr float kStep = 0.5f;
        float prevT = 0.0f;
        float prevAbove = o.y - SampleHeight(o.x, o.z); // > 0 above the surface
        for (float t = kStep; t <= maxDist; t += kStep)
        {
            const vec3 p = o + dir * t;
            const float above = p.y - SampleHeight(p.x, p.z);
            if (prevAbove > 0.0f && above <= 0.0f) // crossed the surface between prevT and t
            {
                const float f = prevAbove / (prevAbove - above);
                hitPoint = o + dir * (prevT + f * (t - prevT));
                constexpr float e = 0.5f;
                const float hL = SampleHeight(hitPoint.x - e, hitPoint.z), hR = SampleHeight(hitPoint.x + e, hitPoint.z);
                const float hD = SampleHeight(hitPoint.x, hitPoint.z - e), hU = SampleHeight(hitPoint.x, hitPoint.z + e);
                hitNormal = glm::normalize(vec3(hL - hR, 2.0f * e, hD - hU));
                return true;
            }
            prevAbove = above;
            prevT = t;
        }
        return false;
    }

    bool TerrainWorld::IsSolidCell(int x, int y, int z) const
    {
        return (float)y + 0.5f < SampleHeight((float)x + 0.5f, (float)z + 0.5f);
    }

    void TerrainWorld::SetPhysicsEnabled(bool enabled)
    {
        m_cfg.physics = enabled;
        UpdateCollider();
    }

    void TerrainWorld::SetTerrainMaterial(float friction, float restitution)
    {
        m_cfg.physicsFriction = friction;
        m_cfg.physicsRestitution = restitution;
#ifdef PE_PHYSICS
        if (m_colliderActive && m_hostNode)
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->SetBodyMaterial(m_hostNode, friction, restitution);
#endif
    }

    void TerrainWorld::UpdateCollider()
    {
#ifdef PE_PHYSICS
        auto *physics = GetGlobalSystem<PhysicsSystem>();
        if (!physics || !m_scene)
            return;
        const bool want =
            m_cfg.physics && m_hostNode && m_scene->IsNodeAlive(m_hostNode) && !m_meshIndices.empty();
        // The tiles are replaced on every rebuild/sculpt, so drop any cached shape first, then re-cook.
        if (m_colliderActive)
        {
            physics->RemoveBody(m_hostNode);
            m_colliderActive = false;
        }
        if (want)
        {
            PhysicsBodyDesc desc;
            desc.bodyType = PhysicsBodyType::Static;
            desc.shapeType = PhysicsShapeType::Mesh;
            desc.autoFitShape = false; // the Mesh shape reads the tiles directly; no box auto-fit
            desc.friction = m_cfg.physicsFriction;
            desc.restitution = m_cfg.physicsRestitution;
            physics->AddBody(*m_scene, m_hostNode, desc);
            m_colliderActive = true;
        }
#endif
    }

    bool TerrainWorld::IsAlive() const
    {
        if (!m_scene)
            return false;
        return m_hostNode != nullptr && m_scene->IsNodeAlive(m_hostNode) && !m_meshIndices.empty() &&
               m_scene->IsValidMeshIndex(m_meshIndices.front());
    }

    void TerrainWorld::Destroy()
    {
        for (int idx : m_meshIndices)
            if (m_scene && idx >= 0 && m_scene->IsValidMeshIndex(idx))
                m_scene->GetMesh(idx).material = nullptr;
        m_meshIndices.clear();

#ifdef PE_PHYSICS
        if (m_colliderActive && m_scene && m_hostNode)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->RemoveBody(m_hostNode);
            m_colliderActive = false;
        }
#endif
        if (m_scene && m_hostNode && m_scene->IsNodeAlive(m_hostNode))
            m_scene->DeleteNode(m_hostNode);

        m_material.reset();
        m_hostNode = nullptr;
        m_height.clear();
        m_pendingSculpts.clear();
        m_scene = nullptr;
    }
} // namespace pe::terrain
