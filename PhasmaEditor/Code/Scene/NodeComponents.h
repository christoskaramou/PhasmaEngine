#pragma once

namespace pe
{
    struct NodeId;
    class Camera;

    // --- ECS node components ---

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

    // --- Subsystem tag/link components ---

    class NodeCameraTag : public IComponent
    {
    public:
        Camera *camera = nullptr;
    };

    class NodeLightTag : public IComponent
    {
    };

    class NodePhysicsTag : public IComponent
    {
    };

    class NodeAudioTag : public IComponent
    {
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
        NodeCameraTag *camera = nullptr;
        NodeLightTag *light = nullptr;
        NodePhysicsTag *physics = nullptr;
        NodeAudioTag *audio = nullptr;
    };
} // namespace pe
