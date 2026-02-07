// Primitives.cpp
#include "Scene/Primitives.h"
#include "API/RHI.h"
#include "Scene/Model.h"

namespace pe
{
    static const float PI = 3.14159265359f;

    static inline glm::vec3 VPos(const Vertex &v) { return {v.position[0], v.position[1], v.position[2]}; }
    static inline glm::vec3 VNor(const Vertex &v) { return {v.normals[0], v.normals[1], v.normals[2]}; }

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

    Model *Primitives::CreatePrimitiveModel(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices)
    {
        Model *model = new Model();

        // Data
        model->m_vertices = vertices;
        model->m_indices = indices;

        // Enforce CCW visibility with your current pipeline state.
        ForceWindingCCW(model->m_vertices, model->m_indices);

        // PositionUvs
        model->m_positionUvs.resize(model->m_vertices.size());
        for (size_t i = 0; i < model->m_vertices.size(); i++)
        {
            model->m_positionUvs[i].position[0] = model->m_vertices[i].position[0];
            model->m_positionUvs[i].position[1] = model->m_vertices[i].position[1];
            model->m_positionUvs[i].position[2] = model->m_vertices[i].position[2];
            model->m_positionUvs[i].uv[0] = model->m_vertices[i].uv[0];
            model->m_positionUvs[i].uv[1] = model->m_vertices[i].uv[1];
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
        meshInfo.renderType = RenderType::Opaque;

        // Default Material
        auto &defaults = Model::GetDefaultResources();
        meshInfo.images[0] = defaults.white;  // BaseColor
        meshInfo.images[1] = defaults.normal; // Normal
        meshInfo.images[2] = defaults.white;  // MetallicRoughness
        meshInfo.images[3] = defaults.white;  // Occlusion
        meshInfo.images[4] = defaults.black;  // Emissive

        meshInfo.samplers[0] = defaults.sampler;
        meshInfo.samplers[1] = defaults.sampler;
        meshInfo.samplers[2] = defaults.sampler;
        meshInfo.samplers[3] = defaults.sampler;
        meshInfo.samplers[4] = defaults.sampler;

        meshInfo.textureMask = 0;

        // Row 0: Base Color (RGBA)
        meshInfo.materialFactors[0][0] = vec4(1.0f, 1.0f, 1.0f, 1.0f);
        // Row 1: Emissive (RGB) + Transmission (A)
        meshInfo.materialFactors[0][1] = vec4(0.0f, 0.0f, 0.0f, 0.0f);
        // Row 2: Metallic, Roughness, AlphaCutoff, OcclusionStrength
        meshInfo.materialFactors[0][2] = vec4(1.0f, 1.0f, 0.5f, 1.0f);
        // Row 3: Unused, NormalScale, Unused, Unused
        meshInfo.materialFactors[0][3] = vec4(0.0f, 1.0f, 0.0f, 0.0f);

        model->m_meshInfos.push_back(meshInfo);

        // NodeInfo
        NodeInfo nodeInfo{};
        nodeInfo.name = model->GetLabel();
        nodeInfo.worldBoundingBox = aabb;
        nodeInfo.localMatrix = mat4(1.0f);
        nodeInfo.dirty = true;
        nodeInfo.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), true);

        model->m_nodeInfos.push_back(nodeInfo);
        model->m_nodeToMesh.push_back(0); // Node 0 -> Mesh 0
        model->m_nodesMoved.push_back(0);

        model->SetRenderReady(true);
        return model;
    }

    Model *Primitives::CreateQuad(float width, float height)
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

        Model *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Quad");
        return model;
    }

    Model *Primitives::CreatePlane(float width, float depth)
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

        Model *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Plane");
        return model;
    }

    Model *Primitives::CreateCube(float size)
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
            FillVertexPosition(v0, p0.x, p0.y, p0.z); FillVertexNormal(v0, n.x, n.y, n.z); FillVertexUV(v0, uv0.x, uv0.y);
            FillVertexPosition(v1, p1.x, p1.y, p1.z); FillVertexNormal(v1, n.x, n.y, n.z); FillVertexUV(v1, uv1.x, uv1.y);
            FillVertexPosition(v2, p2.x, p2.y, p2.z); FillVertexNormal(v2, n.x, n.y, n.z); FillVertexUV(v2, uv2.x, uv2.y);
            FillVertexPosition(v3, p3.x, p3.y, p3.z); FillVertexNormal(v3, n.x, n.y, n.z); FillVertexUV(v3, uv3.x, uv3.y);

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

        Model *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Cube");
        return model;
    }

    Model *Primitives::CreateSphere(float radius, int slices, int stacks)
    {
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
                FillVertexUV(v, U, V);
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

        Model *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Sphere");
        return model;
    }

    Model *Primitives::CreateCylinder(float radius, float height, int slices)
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

        Model *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Cylinder");
        return model;
    }

    Model *Primitives::CreateCone(float radius, float height, int slices)
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

        Model *model = CreatePrimitiveModel(vertices, indices);
        model->SetLabel("Cone");
        return model;
    }

} // namespace pe
