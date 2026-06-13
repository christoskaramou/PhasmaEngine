// Primitives.cpp
#include "Scene/Primitives.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/PassInfoAsset.h"
#include "API/Image.h"

namespace pe
{
    static const float PI = 3.14159265359f;

    static int ClampSegments(int value, int minValue, int maxValue)
    {
        return std::clamp(value, minValue, maxValue);
    }

    static Vertex MakeVertex(const glm::vec3 &pos, const glm::vec3 &normal, const glm::vec2 &uv)
    {
        Vertex v{};
        FillVertexPosition(v, pos.x, pos.y, pos.z);
        FillVertexNormal(v, normal.x, normal.y, normal.z);
        FillVertexUV(v, uv.x, uv.y);
        FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);
        return v;
    }

    static float MirrorLongitudeU(float u)
    {
        return 1.0f - u;
    }

    static glm::vec2 SphericalUV(const glm::vec3 &normal)
    {
        const float u = MirrorLongitudeU(0.5f + std::atan2(normal.z, normal.x) / (2.0f * PI));
        const float v = 0.5f - std::asin(std::clamp(normal.y, -1.0f, 1.0f)) / PI;
        return {u, v};
    }

    static inline glm::vec3 VPos(const Vertex &v)
    {
        return {v.position[0], v.position[1], v.position[2]};
    }
    static inline glm::vec3 VNor(const Vertex &v)
    {
        return {v.normals[0], v.normals[1], v.normals[2]};
    }

    static int ChooseSkinnedStripRows(float width, float height, int segments)
    {
        const float aspectRows = static_cast<float>(segments) * height / std::max(width, 0.01f);
        return std::clamp(static_cast<int>(std::round(aspectRows)), 4, 12) + 1;
    }

    // Your current raster state: FrontFace = CW, CullMode = Front => CW is culled, CCW is visible.
    // So we enforce *CCW* winding for all primitive triangles.
    static void ForceWindingCCW(const std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
    {
        if (indices.size() < 3)
            return;

        for (size_t t = 0; t + 2 < indices.size(); t += 3)
        {
            uint32_t i0 = indices[t + 0];
            uint32_t i1 = indices[t + 1];
            uint32_t i2 = indices[t + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                continue;

            const glm::vec3 p0 = VPos(vertices[i0]);
            const glm::vec3 p1 = VPos(vertices[i1]);
            const glm::vec3 p2 = VPos(vertices[i2]);

            glm::vec3 geomN = glm::cross(p1 - p0, p2 - p0); // RH rule => CCW
            float len2 = glm::dot(geomN, geomN);
            if (len2 < 1e-20f) // degenerate
                continue;
            geomN *= glm::inversesqrt(len2);

            glm::vec3 avgN = VNor(vertices[i0]) + VNor(vertices[i1]) + VNor(vertices[i2]);
            float avgLen2 = glm::dot(avgN, avgN);
            if (avgLen2 < 1e-20f)
                continue;
            avgN *= glm::inversesqrt(avgLen2);

            // If geomN points opposite to intended (vertex) normal, tri is CW relative to it -> swap to make CCW.
            if (glm::dot(geomN, avgN) < 0.0f)
                std::swap(indices[t + 1], indices[t + 2]);
        }
    }

    static void FillAabbVertices(std::vector<AabbVertex> &aabbVertices, const AABB &aabb)
    {
        aabbVertices.resize(8);
        const vec3 corners[8] = {
            {aabb.min.x, aabb.min.y, aabb.min.z},
            {aabb.max.x, aabb.min.y, aabb.min.z},
            {aabb.max.x, aabb.max.y, aabb.min.z},
            {aabb.min.x, aabb.max.y, aabb.min.z},
            {aabb.min.x, aabb.min.y, aabb.max.z},
            {aabb.max.x, aabb.min.y, aabb.max.z},
            {aabb.max.x, aabb.max.y, aabb.max.z},
            {aabb.min.x, aabb.max.y, aabb.max.z}};

        for (int i = 0; i < 8; i++)
        {
            aabbVertices[i].position[0] = corners[i].x;
            aabbVertices[i].position[1] = corners[i].y;
            aabbVertices[i].position[2] = corners[i].z;
        }
    }

    ModelAsset *Primitives::CreatePrimitiveModel(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices, bool lineTopology)
    {
        ModelAsset *model = new ModelAsset();

        // Data
        model->m_vertices = vertices;
        model->m_indices = indices;

        // Enforce CCW visibility with your current pipeline state.
        // Line indices are not triangle triples - reordering them corrupts the strip.
        if (!lineTopology)
            ForceWindingCCW(model->m_vertices, model->m_indices);

        for (auto &v : model->m_vertices)
        {
            v.joints[0] = 0;
            v.joints[1] = 0;
            v.joints[2] = 0;
            v.joints[3] = 0;
            v.weights[0] = 0.f;
            v.weights[1] = 0.f;
            v.weights[2] = 0.f;
            v.weights[3] = 0.f;
        }

        // PositionUvs
        model->m_positionUvs.resize(model->m_vertices.size());
        for (size_t i = 0; i < model->m_vertices.size(); i++)
        {
            auto &pv = model->m_positionUvs[i];
            const auto &sv = model->m_vertices[i];
            pv.position[0] = sv.position[0];
            pv.position[1] = sv.position[1];
            pv.position[2] = sv.position[2];
            pv.uv[0] = sv.uv[0];
            pv.uv[1] = sv.uv[1];
            pv.joints[0] = 0;
            pv.joints[1] = 0;
            pv.joints[2] = 0;
            pv.joints[3] = 0;
            pv.weights[0] = 0.f;
            pv.weights[1] = 0.f;
            pv.weights[2] = 0.f;
            pv.weights[3] = 0.f;
        }

        // AABB
        AABB aabb{};
        aabb.min = vec3(FLT_MAX);
        aabb.max = vec3(-FLT_MAX);

        for (const auto &v : model->m_vertices)
        {
            vec3 pos(v.position[0], v.position[1], v.position[2]);
            aabb.min = glm::min(aabb.min, pos);
            aabb.max = glm::max(aabb.max, pos);
        }

        // AABB Vertices (8 corners)
        model->m_aabbVertices.resize(8);
        vec3 corners[8] = {
            {aabb.min.x, aabb.min.y, aabb.min.z},
            {aabb.max.x, aabb.min.y, aabb.min.z},
            {aabb.max.x, aabb.max.y, aabb.min.z},
            {aabb.min.x, aabb.max.y, aabb.min.z},
            {aabb.min.x, aabb.min.y, aabb.max.z},
            {aabb.max.x, aabb.min.y, aabb.max.z},
            {aabb.max.x, aabb.max.y, aabb.max.z},
            {aabb.min.x, aabb.max.y, aabb.max.z}};

        for (int i = 0; i < 8; i++)
        {
            model->m_aabbVertices[i].position[0] = corners[i].x;
            model->m_aabbVertices[i].position[1] = corners[i].y;
            model->m_aabbVertices[i].position[2] = corners[i].z;
        }

        // MeshInfo
        MeshInfo meshInfo{};
        meshInfo.indicesCount = static_cast<uint32_t>(model->m_indices.size());
        meshInfo.verticesCount = static_cast<uint32_t>(model->m_vertices.size());
        meshInfo.boundingBox = aabb;
        meshInfo.renderType = lineTopology ? RenderType::Lines : RenderType::Opaque;

        // Default Material
        auto &defaults = ModelAsset::GetDefaultResources();
        auto mat = std::make_unique<Material>();
        mat->name = "Default";
        mat->textures[static_cast<int>(TextureType::BaseColor)] = ResourceHandle<Image>::FromRaw(defaults.white);
        mat->textures[static_cast<int>(TextureType::Normal)] = ResourceHandle<Image>::FromRaw(defaults.normal);
        mat->textures[static_cast<int>(TextureType::MetallicRoughness)] = ResourceHandle<Image>::FromRaw(defaults.white);
        mat->textures[static_cast<int>(TextureType::Occlusion)] = ResourceHandle<Image>::FromRaw(defaults.white);
        mat->textures[static_cast<int>(TextureType::Emissive)] = ResourceHandle<Image>::FromRaw(defaults.black);
        for (auto &s : mat->samplers)
            s = defaults.sampler;
        mat->textureMask = 0;
        // PBR defaults: white base color, metallic=0, roughness=1, occlusionStrength=1
        mat->metallic = 0.f;
        mat->roughness = 1.f;
        mat->occlusionStrength = 1.f;
        mat->renderType = lineTopology ? RenderType::Lines : RenderType::Opaque;
        if (!mat->passInfoAsset)
            mat->passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "PassInfo/standard_pbr.pass");
        mat->SyncParamsFromLegacy();

        meshInfo.material = mat.get();
        model->m_materials.push_back(std::move(mat));

        model->m_meshInfos.push_back(meshInfo);

        model->CreateNode(model->GetLabel(), -1, mat4(1.0f), 0);

        return model;
    }

    ModelAsset *Primitives::CreateQuad(float width, float height)
    {
        float w = width * 0.5f;
        float h = height * 0.5f;

        std::vector<Vertex> vertices(4);

        // Layout (XY plane, +Z normal):
        // 0 TL, 1 BL, 2 BR, 3 TR
        FillVertexPosition(vertices[0], -w, +h, 0.0f);
        FillVertexNormal(vertices[0], 0.0f, 0.0f, 1.0f);
        FillVertexUV(vertices[0], 0.0f, 0.0f);

        FillVertexPosition(vertices[1], -w, -h, 0.0f);
        FillVertexNormal(vertices[1], 0.0f, 0.0f, 1.0f);
        FillVertexUV(vertices[1], 0.0f, 1.0f);

        FillVertexPosition(vertices[2], +w, -h, 0.0f);
        FillVertexNormal(vertices[2], 0.0f, 0.0f, 1.0f);
        FillVertexUV(vertices[2], 1.0f, 1.0f);

        FillVertexPosition(vertices[3], +w, +h, 0.0f);
        FillVertexNormal(vertices[3], 0.0f, 0.0f, 1.0f);
        FillVertexUV(vertices[3], 1.0f, 0.0f);

        for (auto &v : vertices)
            FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);

        // CCW when viewed from +Z (visible with your CullFront+FrontFace=CW setup)
        std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Quad");
        model->SetPrimitiveType("quad");
        model->SetPrimitiveParams(vec4(width, height, 0.f, 0.f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Quad");
        return model;
    }

    ModelAsset *Primitives::CreatePlane(float width, float depth)
    {
        float w = width * 0.5f;
        float d = depth * 0.5f;

        std::vector<Vertex> vertices(4);

        // Layout (XZ plane, +Y normal):
        // 0 (-w,-d), 1 (-w,+d), 2 (+w,+d), 3 (+w,-d)
        FillVertexPosition(vertices[0], -w, 0.0f, -d);
        FillVertexNormal(vertices[0], 0.0f, 1.0f, 0.0f);
        FillVertexUV(vertices[0], 0.0f, 0.0f);

        FillVertexPosition(vertices[1], -w, 0.0f, +d);
        FillVertexNormal(vertices[1], 0.0f, 1.0f, 0.0f);
        FillVertexUV(vertices[1], 0.0f, 1.0f);

        FillVertexPosition(vertices[2], +w, 0.0f, +d);
        FillVertexNormal(vertices[2], 0.0f, 1.0f, 0.0f);
        FillVertexUV(vertices[2], 1.0f, 1.0f);

        FillVertexPosition(vertices[3], +w, 0.0f, -d);
        FillVertexNormal(vertices[3], 0.0f, 1.0f, 0.0f);
        FillVertexUV(vertices[3], 1.0f, 0.0f);

        for (auto &v : vertices)
            FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);

        // CCW when viewed from +Y
        std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Plane");
        model->SetPrimitiveType("plane");
        model->SetPrimitiveParams(vec4(width, depth, 0.f, 0.f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Plane");
        return model;
    }

    ModelAsset *Primitives::CreateGrid(float width, float depth, int subdivisions)
    {
        subdivisions = ClampSegments(subdivisions, 1, 512);

        const float halfW = width * 0.5f;
        const float halfD = depth * 0.5f;
        const int vertsPerSide = subdivisions + 1;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(static_cast<size_t>(vertsPerSide) * static_cast<size_t>(vertsPerSide));
        indices.reserve(static_cast<size_t>(subdivisions) * static_cast<size_t>(subdivisions) * 6u);

        for (int z = 0; z <= subdivisions; ++z)
        {
            const float v = z / static_cast<float>(subdivisions);
            const float pz = -halfD + depth * v;
            for (int x = 0; x <= subdivisions; ++x)
            {
                const float u = x / static_cast<float>(subdivisions);
                const float px = -halfW + width * u;
                vertices.push_back(MakeVertex({px, 0.0f, pz}, {0.0f, 1.0f, 0.0f}, {u, v}));
            }
        }

        for (int z = 0; z < subdivisions; ++z)
        {
            for (int x = 0; x < subdivisions; ++x)
            {
                const uint32_t v00 = static_cast<uint32_t>(z * vertsPerSide + x);
                const uint32_t v01 = static_cast<uint32_t>((z + 1) * vertsPerSide + x);
                const uint32_t v11 = static_cast<uint32_t>((z + 1) * vertsPerSide + x + 1);
                const uint32_t v10 = static_cast<uint32_t>(z * vertsPerSide + x + 1);
                indices.insert(indices.end(), {v00, v01, v11, v00, v11, v10});
            }
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Grid");
        model->SetPrimitiveType("grid");
        model->SetPrimitiveParams(vec4(width, depth, static_cast<float>(subdivisions), 0.f), 3);
        model->SetNodeName(model->GetRootNodeIndex(), "Grid");
        return model;
    }

    ModelAsset *Primitives::CreateCube(float size)
    {
        float s = size * 0.5f;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        auto AddFace = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                           glm::vec3 n,
                           glm::vec2 uv0 = {0, 0}, glm::vec2 uv1 = {0, 1}, glm::vec2 uv2 = {1, 1}, glm::vec2 uv3 = {1, 0})
        {
            uint32_t base = (uint32_t)vertices.size();

            Vertex v0{}, v1{}, v2{}, v3{};
            FillVertexPosition(v0, p0.x, p0.y, p0.z);
            FillVertexNormal(v0, n.x, n.y, n.z);
            FillVertexUV(v0, uv0.x, uv0.y);
            FillVertexPosition(v1, p1.x, p1.y, p1.z);
            FillVertexNormal(v1, n.x, n.y, n.z);
            FillVertexUV(v1, uv1.x, uv1.y);
            FillVertexPosition(v2, p2.x, p2.y, p2.z);
            FillVertexNormal(v2, n.x, n.y, n.z);
            FillVertexUV(v2, uv2.x, uv2.y);
            FillVertexPosition(v3, p3.x, p3.y, p3.z);
            FillVertexNormal(v3, n.x, n.y, n.z);
            FillVertexUV(v3, uv3.x, uv3.y);

            FillVertexColor(v0, 1, 1, 1, 1);
            FillVertexColor(v1, 1, 1, 1, 1);
            FillVertexColor(v2, 1, 1, 1, 1);
            FillVertexColor(v3, 1, 1, 1, 1);

            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);
            vertices.push_back(v3);

            // CCW (p0,p1,p2) and (p0,p2,p3) when viewed from outside
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        };

        // Define faces with CCW order when viewed from outside.

        // +Z (Front)
        AddFace({-s, +s, +s}, {-s, -s, +s}, {+s, -s, +s}, {+s, +s, +s}, {0, 0, 1});

        // -Z (Back)  (note ordering changes to keep CCW from outside looking toward -Z)
        AddFace({+s, +s, -s}, {+s, -s, -s}, {-s, -s, -s}, {-s, +s, -s}, {0, 0, -1});

        // +Y (Top)
        AddFace({-s, +s, -s}, {-s, +s, +s}, {+s, +s, +s}, {+s, +s, -s}, {0, 1, 0});

        // -Y (Bottom)
        AddFace({-s, -s, +s}, {-s, -s, -s}, {+s, -s, -s}, {+s, -s, +s}, {0, -1, 0});

        // +X (Right)
        AddFace({+s, +s, +s}, {+s, -s, +s}, {+s, -s, -s}, {+s, +s, -s}, {1, 0, 0});

        // -X (Left)
        AddFace({-s, +s, -s}, {-s, -s, -s}, {-s, -s, +s}, {-s, +s, +s}, {-1, 0, 0});

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Cube");
        model->SetPrimitiveType("cube");
        model->SetPrimitiveParams(vec4(size, 0.f, 0.f, 0.f), 1);
        model->SetNodeName(model->GetRootNodeIndex(), "Cube");
        return model;
    }

    ModelAsset *Primitives::CreateSphere(float radius, int slices, int stacks)
    {
        ModelAsset *model = CreateUvSphere(radius, slices, stacks);
        model->SetLabel("Sphere");
        model->SetPrimitiveType("sphere");
        model->SetPrimitiveParams(vec4(radius, 0.f, 0.f, 0.f), 1);
        model->SetNodeName(model->GetRootNodeIndex(), "Sphere");
        return model;
    }

    ModelAsset *Primitives::CreateUvSphere(float radius, int slices, int stacks)
    {
        slices = ClampSegments(slices, 3, 512);
        stacks = ClampSegments(stacks, 2, 512);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        for (int i = 0; i <= stacks; ++i)
        {
            float V = i / (float)stacks;
            float phi = V * PI;

            for (int j = 0; j <= slices; ++j)
            {
                float U = j / (float)slices;
                float theta = U * (PI * 2);

                float x = cos(theta) * sin(phi);
                float y = cos(phi);
                float z = sin(theta) * sin(phi);

                Vertex v{};
                FillVertexPosition(v, x * radius, y * radius, z * radius);
                FillVertexNormal(v, x, y, z); // outward
                FillVertexUV(v, MirrorLongitudeU(U), V);
                FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);
                vertices.push_back(v);
            }
        }

        for (int i = 0; i < stacks; ++i)
        {
            for (int j = 0; j < slices; ++j)
            {
                uint32_t p0 = i * (slices + 1) + j;
                uint32_t p1 = p0 + 1;
                uint32_t p2 = (i + 1) * (slices + 1) + j;
                uint32_t p3 = p2 + 1;

                // CCW for outward normals (then CreatePrimitiveModel enforces CCW anyway)
                indices.push_back(p0);
                indices.push_back(p1);
                indices.push_back(p2);

                indices.push_back(p1);
                indices.push_back(p3);
                indices.push_back(p2);
            }
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("UV Sphere");
        model->SetPrimitiveType("uv_sphere");
        model->SetPrimitiveParams(vec4(radius, static_cast<float>(slices), static_cast<float>(stacks), 0.f), 3);
        model->SetNodeName(model->GetRootNodeIndex(), "UV Sphere");
        return model;
    }

    ModelAsset *Primitives::CreateIcoSphere(float radius, int subdivisions)
    {
        subdivisions = ClampSegments(subdivisions, 0, 6);

        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        std::vector<glm::vec3> positions = {
            {-1, +t, 0},
            {+1, +t, 0},
            {-1, -t, 0},
            {+1, -t, 0},
            {0, -1, +t},
            {0, +1, +t},
            {0, -1, -t},
            {0, +1, -t},
            {+t, 0, -1},
            {+t, 0, +1},
            {-t, 0, -1},
            {-t, 0, +1},
        };

        for (auto &p : positions)
            p = glm::normalize(p);

        std::vector<std::array<uint32_t, 3>> faces = {
            std::array<uint32_t, 3>{0, 11, 5},
            {0, 5, 1},
            {0, 1, 7},
            {0, 7, 10},
            {0, 10, 11},
            {1, 5, 9},
            {5, 11, 4},
            {11, 10, 2},
            {10, 7, 6},
            {7, 1, 8},
            {3, 9, 4},
            {3, 4, 2},
            {3, 2, 6},
            {3, 6, 8},
            {3, 8, 9},
            {4, 9, 5},
            {2, 4, 11},
            {6, 2, 10},
            {8, 6, 7},
            {9, 8, 1},
        };

        auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t
        {
            const uint32_t lo = std::min(a, b);
            const uint32_t hi = std::max(a, b);
            return (static_cast<uint64_t>(lo) << 32u) | static_cast<uint64_t>(hi);
        };

        for (int level = 0; level < subdivisions; ++level)
        {
            std::unordered_map<uint64_t, uint32_t> midpointCache;
            std::vector<std::array<uint32_t, 3>> nextFaces;
            nextFaces.reserve(faces.size() * 4u);

            auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t
            {
                const uint64_t key = edgeKey(a, b);
                auto it = midpointCache.find(key);
                if (it != midpointCache.end())
                    return it->second;

                glm::vec3 p = glm::normalize((positions[a] + positions[b]) * 0.5f);
                const uint32_t index = static_cast<uint32_t>(positions.size());
                positions.push_back(p);
                midpointCache.emplace(key, index);
                return index;
            };

            for (const auto &f : faces)
            {
                const uint32_t ab = midpoint(f[0], f[1]);
                const uint32_t bc = midpoint(f[1], f[2]);
                const uint32_t ca = midpoint(f[2], f[0]);
                nextFaces.push_back({f[0], ab, ca});
                nextFaces.push_back({f[1], bc, ab});
                nextFaces.push_back({f[2], ca, bc});
                nextFaces.push_back({ab, bc, ca});
            }
            faces = std::move(nextFaces);
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(positions.size());
        indices.reserve(faces.size() * 3u);

        for (const glm::vec3 &p : positions)
            vertices.push_back(MakeVertex(p * radius, p, SphericalUV(p)));

        for (const auto &f : faces)
            indices.insert(indices.end(), {f[0], f[1], f[2]});

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Ico Sphere");
        model->SetPrimitiveType("ico_sphere");
        model->SetPrimitiveParams(vec4(radius, static_cast<float>(subdivisions), 0.f, 0.f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Ico Sphere");
        return model;
    }

    ModelAsset *Primitives::CreatePyramid(float baseSize, float height)
    {
        const float s = baseSize * 0.5f;
        const float halfH = height * 0.5f;

        const glm::vec3 backLeft = {-s, -halfH, -s};
        const glm::vec3 backRight = {+s, -halfH, -s};
        const glm::vec3 frontRight = {+s, -halfH, +s};
        const glm::vec3 frontLeft = {-s, -halfH, +s};
        const glm::vec3 apex = {0.0f, +halfH, 0.0f};

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(18);
        indices.reserve(18);

        auto addTri = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c,
                          const glm::vec2 &uvA, const glm::vec2 &uvB, const glm::vec2 &uvC)
        {
            glm::vec3 normal = glm::cross(b - a, c - a);
            const float len2 = glm::dot(normal, normal);
            normal = len2 > 1e-20f ? normal * glm::inversesqrt(len2) : glm::vec3(0.0f, 1.0f, 0.0f);
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back(MakeVertex(a, normal, uvA));
            vertices.push_back(MakeVertex(b, normal, uvB));
            vertices.push_back(MakeVertex(c, normal, uvC));
            indices.insert(indices.end(), {base, base + 1u, base + 2u});
        };

        addTri(backLeft, backRight, frontRight, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f});
        addTri(backLeft, frontRight, frontLeft, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
        addTri(frontLeft, frontRight, apex, {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f});
        addTri(frontRight, backRight, apex, {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f});
        addTri(backRight, backLeft, apex, {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f});
        addTri(backLeft, frontLeft, apex, {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f});

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Pyramid");
        model->SetPrimitiveType("pyramid");
        model->SetPrimitiveParams(vec4(baseSize, height, 0.f, 0.f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Pyramid");
        return model;
    }

    ModelAsset *Primitives::CreateCircle(float radius, int segments)
    {
        segments = ClampSegments(segments, 3, 512);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(static_cast<size_t>(segments) + 1u);
        indices.reserve(static_cast<size_t>(segments) * 3u);

        vertices.push_back(MakeVertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f}));
        for (int i = 0; i < segments; ++i)
        {
            const float u = i / static_cast<float>(segments);
            const float theta = u * PI * 2.0f;
            const float x = std::cos(theta);
            const float y = std::sin(theta);
            vertices.push_back(MakeVertex({x * radius, y * radius, 0.0f},
                                          {0.0f, 0.0f, 1.0f},
                                          {(x + 1.0f) * 0.5f, (y + 1.0f) * 0.5f}));
        }

        for (int i = 0; i < segments; ++i)
        {
            const uint32_t a = static_cast<uint32_t>(i + 1);
            const uint32_t b = static_cast<uint32_t>(((i + 1) % segments) + 1);
            indices.insert(indices.end(), {0u, a, b});
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Circle");
        model->SetPrimitiveType("circle");
        model->SetPrimitiveParams(vec4(radius, static_cast<float>(segments), 0.f, 0.f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Circle");
        return model;
    }

    ModelAsset *Primitives::CreateTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments)
    {
        majorSegments = ClampSegments(majorSegments, 3, 512);
        minorSegments = ClampSegments(minorSegments, 3, 256);
        majorRadius = std::max(majorRadius, 0.001f);
        minorRadius = std::max(minorRadius, 0.001f);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(static_cast<size_t>(majorSegments + 1) * static_cast<size_t>(minorSegments + 1));
        indices.reserve(static_cast<size_t>(majorSegments) * static_cast<size_t>(minorSegments) * 6u);

        for (int i = 0; i <= majorSegments; ++i)
        {
            const float u = i / static_cast<float>(majorSegments);
            const float theta = u * PI * 2.0f;
            const float cx = std::cos(theta);
            const float cz = std::sin(theta);

            for (int j = 0; j <= minorSegments; ++j)
            {
                const float v = j / static_cast<float>(minorSegments);
                const float phi = v * PI * 2.0f;
                const float cp = std::cos(phi);
                const float sp = std::sin(phi);
                const glm::vec3 normal = glm::normalize(glm::vec3(cx * cp, sp, cz * cp));
                const glm::vec3 pos = {cx * (majorRadius + minorRadius * cp), minorRadius * sp,
                                       cz * (majorRadius + minorRadius * cp)};
                vertices.push_back(MakeVertex(pos, normal, {u, v}));
            }
        }

        const int ring = minorSegments + 1;
        for (int i = 0; i < majorSegments; ++i)
        {
            for (int j = 0; j < minorSegments; ++j)
            {
                const uint32_t a = static_cast<uint32_t>(i * ring + j);
                const uint32_t b = static_cast<uint32_t>((i + 1) * ring + j);
                const uint32_t c = static_cast<uint32_t>((i + 1) * ring + j + 1);
                const uint32_t d = static_cast<uint32_t>(i * ring + j + 1);
                indices.insert(indices.end(), {a, b, c, a, c, d});
            }
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Torus");
        model->SetPrimitiveType("torus");
        model->SetPrimitiveParams(vec4(majorRadius, minorRadius, static_cast<float>(majorSegments),
                                       static_cast<float>(minorSegments)),
                                  4);
        model->SetNodeName(model->GetRootNodeIndex(), "Torus");
        return model;
    }

    ModelAsset *Primitives::CreatePolylineRibbon(const std::vector<vec3> &points, float width, const vec3 &normal, bool closed)
    {
        const size_t count = points.size();
        if (count < 2)
            return nullptr;

        const vec3 n = normalize(normal);
        const float halfWidth = std::max(width, 1e-6f) * 0.5f;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(count * 2u);
        indices.reserve(count * 12u);

        for (size_t i = 0; i < count; ++i)
        {
            vec3 dir;
            if (closed)
                dir = points[(i + 1) % count] - points[(i + count - 1) % count];
            else if (i == 0)
                dir = points[1] - points[0];
            else if (i == count - 1)
                dir = points[count - 1] - points[count - 2];
            else
                dir = points[i + 1] - points[i - 1];

            const float dirLen = length(dir);
            dir = dirLen > 1e-6f ? dir / dirLen : vec3(1.0f, 0.0f, 0.0f);
            vec3 side = cross(n, dir);
            const float sideLen = length(side);
            side = sideLen > 1e-6f ? side / sideLen : vec3(0.0f, 0.0f, 1.0f);

            const float u = i / static_cast<float>(count);
            vertices.push_back(MakeVertex(points[i] - side * halfWidth, n, {u, 0.0f}));
            vertices.push_back(MakeVertex(points[i] + side * halfWidth, n, {u, 1.0f}));
        }

        const size_t segments = closed ? count : count - 1;
        for (size_t i = 0; i < segments; ++i)
        {
            const uint32_t a = static_cast<uint32_t>(i * 2);
            const uint32_t b = a + 1;
            const uint32_t c = static_cast<uint32_t>(((i + 1) % count) * 2);
            const uint32_t d = c + 1;
            // both windings so the ribbon reads from either side of the plane
            indices.insert(indices.end(), {a, c, b, b, c, d});
            indices.insert(indices.end(), {a, b, c, c, b, d});
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("PolylineRibbon");
        model->SetPrimitiveType("polyline_ribbon");
        model->SetPrimitiveParams(vec4(width, static_cast<float>(count), closed ? 1.0f : 0.0f, 0.0f), 3);
        model->SetNodeName(model->GetRootNodeIndex(), "PolylineRibbon");
        return model;
    }

    ModelAsset *Primitives::CreatePolyline(const std::vector<vec3> &points, bool closed)
    {
        const size_t count = points.size();
        if (count < 2)
            return nullptr;

        std::vector<Vertex> vertices;
        vertices.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const float u = i / static_cast<float>(count);
            vertices.push_back(MakeVertex(points[i], vec3(0.0f, 1.0f, 0.0f), {u, 0.0f}));
        }

        // Line-strip indices; a closed loop repeats the first point.
        std::vector<uint32_t> indices;
        indices.reserve(count + (closed ? 1u : 0u));
        for (size_t i = 0; i < count; ++i)
            indices.push_back(static_cast<uint32_t>(i));
        if (closed)
            indices.push_back(0u);

        ModelAsset *model = CreatePrimitiveModel(vertices, indices, true);
        model->SetLabel("Polyline");
        model->SetPrimitiveType("polyline");
        model->SetPrimitiveParams(vec4(static_cast<float>(count), closed ? 1.0f : 0.0f, 0.0f, 0.0f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Polyline");
        return model;
    }

    ModelAsset *Primitives::CreateSkinnedStrip2D(float width, float height, int segments, int bones)
    {
        width = std::max(width, 0.01f);
        height = std::max(height, 0.01f);
        segments = ClampSegments(segments, 1, 512);
        bones = ClampSegments(bones, 2, 64);
        bones = std::max(bones, std::min(segments + 1, 24));

        const int rows = ChooseSkinnedStripRows(width, height, segments);
        const int rowSegments = rows - 1;
        std::vector<Vertex> vertices;
        vertices.resize(static_cast<size_t>(segments + 1) * static_cast<size_t>(rows));

        const float halfWidth = width * 0.5f;
        const float halfHeight = height * 0.5f;
        for (int column = 0; column <= segments; column++)
        {
            const float u = static_cast<float>(column) / static_cast<float>(segments);
            const float x = -halfWidth + u * width;

            for (int row = 0; row < rows; row++)
            {
                const float rowT = static_cast<float>(row) / static_cast<float>(rowSegments);
                const float y = -halfHeight + rowT * height;
                Vertex &vertex = vertices[static_cast<size_t>(column) * static_cast<size_t>(rows) + static_cast<size_t>(row)];
                FillVertexPosition(vertex, x, y, 0.0f);
                FillVertexNormal(vertex, 0.0f, 0.0f, 1.0f);
                FillVertexUV(vertex, u, 1.0f - rowT);
                FillVertexColor(vertex, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(segments) * static_cast<size_t>(rowSegments) * 6);
        for (int column = 0; column < segments; column++)
        {
            for (int row = 0; row < rowSegments; row++)
            {
                const uint32_t v00 = static_cast<uint32_t>(column * rows + row);
                const uint32_t v01 = v00 + 1;
                const uint32_t v10 = static_cast<uint32_t>((column + 1) * rows + row);
                const uint32_t v11 = v10 + 1;

                indices.push_back(v00);
                indices.push_back(v10);
                indices.push_back(v11);
                indices.push_back(v00);
                indices.push_back(v11);
                indices.push_back(v01);
            }
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Skinned Strip 2D");
        model->SetPrimitiveType("skinned_strip_2d");
        model->SetPrimitiveParams(vec4(width, height, static_cast<float>(segments), static_cast<float>(bones)), 4);
        model->SetNodeName(model->GetRootNodeIndex(), "Skinned Strip 2D");

        MeshInfo *meshInfo = model->GetMeshInfo(0);
        if (meshInfo)
        {
            meshInfo->skinned = true;
            const float deformationPad = width + height;
            meshInfo->boundingBox.min = vec3(-halfWidth - deformationPad, -halfHeight - deformationPad, -0.01f);
            meshInfo->boundingBox.max = vec3(halfWidth + deformationPad, halfHeight + deformationPad, 0.01f);
            FillAabbVertices(model->m_aabbVertices, meshInfo->boundingBox);
        }

        const float boneStep = width / static_cast<float>(bones - 1);
        model->m_skeleton.bones.reserve(static_cast<size_t>(bones));
        for (int i = 0; i < bones; i++)
        {
            BoneInfo bone{};
            bone.name = "strip2d_" + std::to_string(i);
            bone.parentIndex = i == 0 ? -1 : i - 1;

            const float bindX = -halfWidth + static_cast<float>(i) * boneStep;
            const float localX = i == 0 ? -halfWidth : boneStep;
            bone.localBindTransform = glm::translate(mat4(1.0f), vec3(localX, 0.0f, 0.0f));
            bone.offsetMatrix = glm::inverse(glm::translate(mat4(1.0f), vec3(bindX, 0.0f, 0.0f)));
            bone.intermediatePrefix = mat4(1.0f);

            const int boneIndex = static_cast<int>(model->m_skeleton.bones.size());
            model->m_skeleton.boneNameToIndex[bone.name] = boneIndex;
            model->m_skeleton.bones.push_back(bone);
        }

        auto assignCurveWeights = [&](size_t vertexIndex, float x)
        {
            const float u = std::clamp((x + halfWidth) / width, 0.0f, 1.0f);
            const float scaled = u * static_cast<float>(bones - 1);
            int j1 = static_cast<int>(std::floor(scaled));
            float t = scaled - static_cast<float>(j1);
            if (j1 >= bones - 1)
            {
                j1 = bones - 2;
                t = 1.0f;
            }

            const float t2 = t * t;
            const float t3 = t2 * t;
            const int joints[4] = {
                std::max(j1 - 1, 0),
                j1,
                std::min(j1 + 1, bones - 1),
                std::min(j1 + 2, bones - 1),
            };
            const float weights[4] = {
                -0.5f * t + t2 - 0.5f * t3,
                1.0f - 2.5f * t2 + 1.5f * t3,
                0.5f * t + 2.0f * t2 - 1.5f * t3,
                -0.5f * t2 + 0.5f * t3,
            };

            FillVertexJointsWeights(model->m_vertices[vertexIndex],
                                    static_cast<uint8_t>(joints[0]),
                                    static_cast<uint8_t>(joints[1]),
                                    static_cast<uint8_t>(joints[2]),
                                    static_cast<uint8_t>(joints[3]),
                                    weights[0],
                                    weights[1],
                                    weights[2],
                                    weights[3]);
            FillVertexJointsWeights(model->m_positionUvs[vertexIndex],
                                    static_cast<uint8_t>(joints[0]),
                                    static_cast<uint8_t>(joints[1]),
                                    static_cast<uint8_t>(joints[2]),
                                    static_cast<uint8_t>(joints[3]),
                                    weights[0],
                                    weights[1],
                                    weights[2],
                                    weights[3]);
        };

        for (int column = 0; column <= segments; column++)
            for (int row = 0; row < rows; row++)
            {
                const size_t index = static_cast<size_t>(column) * static_cast<size_t>(rows) + static_cast<size_t>(row);
                assignCurveWeights(index, model->m_vertices[index].position[0]);
            }

        AnimationClip wave{};
        wave.name = "wave";
        wave.duration = 60.0f;
        wave.ticksPerSecond = 60.0f;
        wave.channels.reserve(static_cast<size_t>(bones));

        static constexpr int KEY_COUNT = 9;
        for (int i = 0; i < bones; i++)
        {
            AnimationChannel channel{};
            channel.boneIndex = i;

            const float localX = i == 0 ? -halfWidth : boneStep;
            const float boneT = bones > 1 ? static_cast<float>(i) / static_cast<float>(bones - 1) : 0.0f;
            for (int key = 0; key < KEY_COUNT; key++)
            {
                const float t = wave.duration * static_cast<float>(key) / static_cast<float>(KEY_COUNT - 1);
                const float phase = (t / wave.duration) * PI * 2.0f + boneT * PI * 1.25f;
                const float angle = std::sin(phase) * glm::radians(16.0f) * boneT;

                channel.positionKeys.push_back({t, vec3(localX, 0.0f, 0.0f)});
                channel.rotationKeys.push_back({t, glm::angleAxis(angle, vec3(0.0f, 0.0f, 1.0f))});
                channel.scaleKeys.push_back({t, vec3(1.0f)});
            }

            wave.channels.push_back(std::move(channel));
        }
        model->m_animations.push_back(std::move(wave));

        return model;
    }

    ModelAsset *Primitives::CreateCylinder(float radius, float height, int slices)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float halfH = height * 0.5f;

        // Side vertices: for each slice angle, add top then bottom (radial normals)
        for (int i = 0; i <= slices; ++i)
        {
            float U = i / (float)slices;
            float theta = U * (PI * 2);

            float x = cos(theta);
            float z = sin(theta);

            Vertex vTop{};
            FillVertexPosition(vTop, x * radius, +halfH, z * radius);
            FillVertexNormal(vTop, x, 0.0f, z);
            FillVertexUV(vTop, U, 0.0f);
            FillVertexColor(vTop, 1.f, 1.f, 1.f, 1.f);
            vertices.push_back(vTop);

            Vertex vBot{};
            FillVertexPosition(vBot, x * radius, -halfH, z * radius);
            FillVertexNormal(vBot, x, 0.0f, z);
            FillVertexUV(vBot, U, 1.0f);
            FillVertexColor(vBot, 1.f, 1.f, 1.f, 1.f);
            vertices.push_back(vBot);
        }

        // Side indices (two tris per quad)
        for (int i = 0; i < slices; ++i)
        {
            uint32_t t0 = i * 2;       // top i
            uint32_t b0 = t0 + 1;      // bottom i
            uint32_t t1 = (i + 1) * 2; // top i+1
            uint32_t b1 = t1 + 1;      // bottom i+1

            // Intended CCW from outside
            indices.push_back(t0);
            indices.push_back(b0);
            indices.push_back(t1);

            indices.push_back(t1);
            indices.push_back(b0);
            indices.push_back(b1);
        }

        // --- Caps ---
        // Top cap center
        uint32_t topCenterIndex = (uint32_t)vertices.size();
        Vertex topCenter{};
        FillVertexPosition(topCenter, 0.0f, +halfH, 0.0f);
        FillVertexNormal(topCenter, 0.0f, 1.0f, 0.0f);
        FillVertexUV(topCenter, 0.5f, 0.5f);
        FillVertexColor(topCenter, 1.f, 1.f, 1.f, 1.f);
        vertices.push_back(topCenter);

        // Top cap ring
        uint32_t topRingStart = (uint32_t)vertices.size();
        for (int i = 0; i <= slices; ++i)
        {
            float U = i / (float)slices;
            float theta = U * (PI * 2);
            float x = cos(theta);
            float z = sin(theta);

            Vertex v{};
            FillVertexPosition(v, x * radius, +halfH, z * radius);
            FillVertexNormal(v, 0.0f, 1.0f, 0.0f);
            FillVertexUV(v, (x + 1) * 0.5f, (z + 1) * 0.5f);
            FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);
            vertices.push_back(v);
        }

        // CCW when viewed from +Y
        for (int i = 0; i < slices; ++i)
        {
            indices.push_back(topCenterIndex);
            indices.push_back(topRingStart + i);
            indices.push_back(topRingStart + i + 1);
        }

        // Bottom cap center
        uint32_t bottomCenterIndex = (uint32_t)vertices.size();
        Vertex bottomCenter{};
        FillVertexPosition(bottomCenter, 0.0f, -halfH, 0.0f);
        FillVertexNormal(bottomCenter, 0.0f, -1.0f, 0.0f);
        FillVertexUV(bottomCenter, 0.5f, 0.5f);
        FillVertexColor(bottomCenter, 1.f, 1.f, 1.f, 1.f);
        vertices.push_back(bottomCenter);

        // Bottom cap ring
        uint32_t bottomRingStart = (uint32_t)vertices.size();
        for (int i = 0; i <= slices; ++i)
        {
            float U = i / (float)slices;
            float theta = U * (PI * 2);
            float x = cos(theta);
            float z = sin(theta);

            Vertex v{};
            FillVertexPosition(v, x * radius, -halfH, z * radius);
            FillVertexNormal(v, 0.0f, -1.0f, 0.0f);
            FillVertexUV(v, (x + 1) * 0.5f, (z + 1) * 0.5f);
            FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);
            vertices.push_back(v);
        }

        // For -Y normal, reverse order (still ends up CCW relative to vertex normal after enforcement)
        for (int i = 0; i < slices; ++i)
        {
            indices.push_back(bottomCenterIndex);
            indices.push_back(bottomRingStart + i + 1);
            indices.push_back(bottomRingStart + i);
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Cylinder");
        model->SetPrimitiveType("cylinder");
        model->SetPrimitiveParams(vec4(radius, height, 0.f, 0.f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Cylinder");
        return model;
    }

    ModelAsset *Primitives::CreateCone(float radius, float height, int slices)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float halfH = height * 0.5f;

        // Tip
        Vertex tip{};
        FillVertexPosition(tip, 0.0f, +halfH, 0.0f);
        FillVertexNormal(tip, 0.0f, 1.0f, 0.0f); // not super meaningful at the tip, but ok
        FillVertexUV(tip, 0.5f, 0.0f);
        FillVertexColor(tip, 1.f, 1.f, 1.f, 1.f);
        vertices.push_back(tip);
        uint32_t tipIndex = 0;

        // Side ring (smooth normals)
        uint32_t ringStart = (uint32_t)vertices.size();
        for (int i = 0; i <= slices; ++i)
        {
            float U = i / (float)slices;
            float theta = U * (PI * 2);
            float x = cos(theta);
            float z = sin(theta);

            // approximate cone side normal (outward)
            glm::vec3 n = glm::normalize(glm::vec3(x, radius / height, z));

            Vertex v{};
            FillVertexPosition(v, x * radius, -halfH, z * radius);
            FillVertexNormal(v, n.x, n.y, n.z);
            FillVertexUV(v, U, 1.0f);
            FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);
            vertices.push_back(v);
        }

        // Side triangles (order around the ring; CreatePrimitiveModel will enforce CCW if any end up flipped)
        for (int i = 0; i < slices; ++i)
        {
            uint32_t a = tipIndex;
            uint32_t b = ringStart + i;
            uint32_t c = ringStart + i + 1;

            // intended CCW for outward-ish normals (tip is special anyway)
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(c);
        }

        // Base cap center
        uint32_t baseCenterIndex = (uint32_t)vertices.size();
        Vertex baseCenter{};
        FillVertexPosition(baseCenter, 0.0f, -halfH, 0.0f);
        FillVertexNormal(baseCenter, 0.0f, -1.0f, 0.0f);
        FillVertexUV(baseCenter, 0.5f, 0.5f);
        FillVertexColor(baseCenter, 1.f, 1.f, 1.f, 1.f);
        vertices.push_back(baseCenter);

        // Base cap ring (flat -Y)
        uint32_t baseRingStart = (uint32_t)vertices.size();
        for (int i = 0; i <= slices; ++i)
        {
            float U = i / (float)slices;
            float theta = U * (PI * 2);
            float x = cos(theta);
            float z = sin(theta);

            Vertex v{};
            FillVertexPosition(v, x * radius, -halfH, z * radius);
            FillVertexNormal(v, 0.0f, -1.0f, 0.0f);
            FillVertexUV(v, (x + 1) * 0.5f, (z + 1) * 0.5f);
            FillVertexColor(v, 1.f, 1.f, 1.f, 1.f);
            vertices.push_back(v);
        }

        // -Y cap: reverse order
        for (int i = 0; i < slices; ++i)
        {
            indices.push_back(baseCenterIndex);
            indices.push_back(baseRingStart + i + 1);
            indices.push_back(baseRingStart + i);
        }

        ModelAsset *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Cone");
        model->SetPrimitiveType("cone");
        model->SetPrimitiveParams(vec4(radius, height, 0.f, 0.f), 2);
        model->SetNodeName(model->GetRootNodeIndex(), "Cone");
        return model;
    }

} // namespace pe
