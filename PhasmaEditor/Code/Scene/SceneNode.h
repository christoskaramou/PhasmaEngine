#pragma once

namespace pe
{
    class Image;
    class Sampler;

    // Stable node identity — heap allocated, never moves.
    // All references (parent, children, selection, undo) hold NodeId*.
    // On swap-and-pop deletion, only NodeId::index is updated.
    struct NodeId
    {
        uint32_t index; // current position in Scene's SoA arrays
    };

    // Component presence flags — editor reads these to know which panels to show
    enum ComponentFlags : uint32_t
    {
        Component_None     = 0,
        Component_Mesh     = 1 << 0,
        Component_Light    = 1 << 1,
        Component_Physics  = 1 << 2,
        Component_Camera   = 1 << 3,
    };

    // Lightweight mesh descriptor — references into Scene's data stores
    struct Mesh
    {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t positionsOffset = 0;

        size_t aabbVertexOffset = 0;
        uint32_t aabbColor = 0;
        AABB boundingBox;

        RenderType renderType;
        uint32_t textureMask = 0;

        // Refs into Scene's image/sampler stores
        ResourceHandle<Image> images[5];
        Sampler *samplers[5]{nullptr};

        // Material data
        mat4 materialFactors[2] = {mat4(1.f), mat4(1.f)};
    };

    // Utility: bit mask for texture slots
    inline constexpr uint32_t TextureBit(TextureType type) { return 1u << static_cast<uint32_t>(type); }

    // GPU-side data uploaded per node
    struct NodeGpuData
    {
        mat4 worldMatrix = mat4(1.f);
        mat4 previousWorldMatrix = mat4(1.f);
        mat4 materialFactors[2] = {mat4(1.f), mat4(1.f)};
    };

    // Per-node renderer runtime state (hot path, separate from logical data)
    struct NodeRuntime
    {
        size_t dataOffset = static_cast<size_t>(-1);
        uint32_t indirectIndex = 0;
        int instanceIndex = -1;
        AABB worldAABB;
        NodeGpuData gpuData;
        bool dirty = false;
        std::vector<bool> dirtyUniforms;
    };

    // Image view indices for GPU descriptor binding (per mesh)
    struct MeshRuntime
    {
        uint32_t imageViewIndices[5] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    };
    // Transform an AABB by a matrix (8-corner method)
    inline AABB TransformAabb(const AABB &local, const mat4 &m)
    {
        const vec3 &mn = local.min;
        const vec3 &mx = local.max;

        const vec3 corners[8] = {
            {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
            {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
            {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z},
            {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
        };

        vec3 outMin(std::numeric_limits<float>::max());
        vec3 outMax(-std::numeric_limits<float>::max());

        for (const vec3 &c : corners)
        {
            vec4 t = m * vec4(c, 1.f);
            vec3 p(t.x, t.y, t.z);
            outMin = min(outMin, p);
            outMax = max(outMax, p);
        }

        return AABB{outMin, outMax};
    }
} // namespace pe
