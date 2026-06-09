#pragma once

namespace pe
{
    class Entity;
    class Image;
    class Material;
    class MaterialInstance;
    class Sampler;

    // Stable node identity — heap allocated, never moves while alive.
    // Scene-wide rebuilds retire old ids so stale editor pointers can fail
    // liveness checks without dereferencing freed memory.
    // On swap-and-pop deletion, only NodeId::index is updated.
    struct NodeId
    {
        Entity *entity = nullptr;
        uint32_t index;
        uint32_t revision = 0;
    };

    // Component presence flags — editor reads these to know which panels to show
    enum ComponentFlags : uint32_t
    {
        Component_None = 0,
        Component_Mesh = 1 << 0,
        Component_Light = 1 << 1,
        Component_Physics = 1 << 2,
        Component_Camera = 1 << 3,
        Component_Script = 1 << 4,
        Component_Audio = 1 << 5,
        Component_GpuPending = 1 << 6, // Node geometry not yet uploaded to GPU
        Component_Skybox = 1 << 8,
        Component_RuntimeUi = 1 << 9,
        Component_Physics2D = 1 << 10,
        Component_Prefab = 1 << 11,
        Component_Sprite = 1 << 12,
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

        // First-class material reference
        Material *material = nullptr;
        MaterialInstance *materialInstance = nullptr;

        bool skinned = false;
        bool live = true;
    };

    // Utility: bit mask for texture slots
    inline constexpr uint32_t TextureBit(TextureType type)
    {
        return 1u << static_cast<uint32_t>(type);
    }

    // GPU-side data uploaded per node (world + previous world matrices only;
    // material data lives in the material table StructuredBuffer).
    struct NodeGpuData
    {
        mat4 worldMatrix = mat4(1.f);
        mat4 previousWorldMatrix = mat4(1.f);
    };

    struct NodeRuntime
    {
        size_t dataOffset = static_cast<size_t>(-1);
        std::vector<uint32_t> meshRefIndirect; // parallel to meshRefs
        std::vector<int> rtInstanceIndices;    // parallel to meshRefs
        AABB worldAABB;
        NodeGpuData gpuData;
        bool dirty = false;
        bool gpuPending = false;
        bool hasUniformData = false; // cached: node has at least one drawable mesh
        uint8_t dirtyUniforms = 0;   // bitmask per swapchain frame (max 8); replaces vector<bool>

        std::vector<mat4> jointMatrices;
    };

    // Image view indices and material GPU index (per mesh)
    struct MeshRuntime
    {
        uint32_t imageViewIndices[5] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        uint32_t materialGpuIndex = 0xFFFFFFFF;
    };
    // Transform an AABB by a matrix (8-corner method)
    inline AABB TransformAabb(const AABB &local, const mat4 &m)
    {
        const vec3 &mn = local.min;
        const vec3 &mx = local.max;

        const vec3 corners[8] = {
            {mn.x, mn.y, mn.z},
            {mx.x, mn.y, mn.z},
            {mx.x, mx.y, mn.z},
            {mn.x, mx.y, mn.z},
            {mn.x, mn.y, mx.z},
            {mx.x, mn.y, mx.z},
            {mx.x, mx.y, mx.z},
            {mn.x, mx.y, mx.z},
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
