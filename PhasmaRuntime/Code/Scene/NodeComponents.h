#pragma once

namespace pe
{
    struct NodeId;
    class Camera;

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
        bool enabled = true;
    };

    class NodeTransformComponent : public IComponent
    {
    public:
        mat4 localMatrix = mat4(1.f);
    };

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

    class NodePhysics2DTag : public IComponent
    {
    };

    class NodeAudioTag : public IComponent
    {
    };

    class NodeSkyboxTag : public IComponent
    {
    public:
        std::string path;
    };

    class NodeRuntimeUiTag : public IComponent
    {
    };

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
        NodePhysics2DTag *physics2d = nullptr;
        NodeAudioTag *audio = nullptr;
        NodeSkyboxTag *skybox = nullptr;
        NodeRuntimeUiTag *runtimeUi = nullptr;
    };
} // namespace pe
