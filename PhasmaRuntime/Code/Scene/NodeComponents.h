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

    class NodeAudioTag : public IComponent
    {
    };

    enum class SpriteCoordinateSpace
    {
        World,
        NDC
    };

    class NodeSpriteTag : public IComponent
    {
    public:
        SpriteCoordinateSpace coordinateSpace = SpriteCoordinateSpace::World;
        vec2 size = vec2(1.0f);
        vec2 ndcPosition = vec2(0.0f);
        vec2 ndcSize = vec2(0.25f);
        float ndcDepth = 1.0f;
        float ndcRotation = 0.0f;
        vec4 uvRect = vec4(0.0f, 0.0f, 1.0f, 1.0f);
        int atlasColumns = 1;
        int atlasRows = 1;
        int frame = 0;
        float animationFps = 0.0f;
        bool animationLoop = true;
        bool animationPlaying = false;
        float animationTime = 0.0f;
        std::vector<int> animationFrames;
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
        NodeAudioTag *audio = nullptr;
        NodeSpriteTag *sprite = nullptr;
    };
} // namespace pe
