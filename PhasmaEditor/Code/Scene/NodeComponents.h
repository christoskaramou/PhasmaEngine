#pragma once

namespace pe
{
    struct NodeId;

    // --- ECS node components (Phase 1: dual-written alongside SoA; source of truth after Phase 2) ---

    class NodeNameComponent : public IComponent
    {
    public:
        std::string name;
    };

    class NodeHierarchyComponent : public IComponent
    {
    public:
        NodeId *parent = nullptr;
        std::vector<NodeId *> children;
    };

    class NodeTransformComponent : public IComponent
    {
    public:
        mat4 localMatrix = mat4(1.f);
    };

    // Vector-backed from day one to enable future 0..N meshes per node.
    // Current code treats slot 0 as "the mesh" (empty = no mesh).
    class NodeMeshRefsComponent : public IComponent
    {
    public:
        std::vector<int> meshRefs;
    };

    class NodeScriptComponent : public IComponent
    {
    public:
        std::string path;
    };

    // Fast indexed access to node components — avoids per-frame GetComponent<T>() hashing.
    // Rebuilt on node creation/deletion; slots indexed by NodeId::index.
    struct NodeComponentCache
    {
        NodeNameComponent *name = nullptr;
        NodeHierarchyComponent *hierarchy = nullptr;
        NodeTransformComponent *transform = nullptr;
        NodeMeshRefsComponent *meshRefs = nullptr;
        NodeScriptComponent *script = nullptr;
    };
} // namespace pe
