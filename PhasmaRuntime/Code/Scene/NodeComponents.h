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

    class NodeSkinnedStrip2DComponent : public IComponent
    {
    public:
        std::vector<float> rotationsRadians;
        std::vector<float> jointInfluences;
        std::vector<float> widthScales;
        vec2 ikTargetLocal = vec2(1.0f, 0.0f);
        int ikIterations = 8;
        float maxBendDegrees = 60.0f;
        float bendSign = 1.0f;
        float stretchScale = 1.0f;
        float maxStretchScale = 1.5f;
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

    class NodePrefabComponent : public IComponent
    {
    public:
        std::string path;
    };

    struct NodeSpriteFrame
    {
        std::string name;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        float pivotX = 0.5f;
        float pivotY = 0.5f;
        float duration = 0.1f;
        vec4 uvRect = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };

    struct NodeSpriteClip
    {
        std::string name;
        int start = 0;
        int end = 0;
        float fps = 10.0f;
        bool loop = true;
    };

    class NodeSpriteComponent : public IComponent
    {
    public:
        std::string imagePath;
        std::string metadataPath;
        std::string frameName;
        int frameIndex = -1;
        int imageWidth = 0;
        int imageHeight = 0;
        int frameWidth = 0;
        int frameHeight = 0;
        float quadWidth = 1.0f;
        float quadHeight = 1.0f;
        vec4 uvRect = vec4(0.0f, 0.0f, 1.0f, 1.0f);
        vec4 tint = vec4(1.0f);
        std::vector<NodeSpriteFrame> frames;
        std::vector<NodeSpriteClip> clips;
        std::string activeClipName;
        int activeClipIndex = -1;
        int meshSlot = 0;
        float playbackAccumulator = 0.0f;
        float playbackSpeed = 1.0f;
        bool playing = false;
        bool loop = true;
        bool metadataLoaded = false;
    };

    struct NodeComponentCache
    {
        NodeNameComponent *name = nullptr;
        NodeHierarchyComponent *hierarchy = nullptr;
        NodeTransformComponent *transform = nullptr;
        NodeMeshRefsComponent *meshRefs = nullptr;
        NodeScriptComponent *script = nullptr;
        NodeSkinnedStrip2DComponent *skinnedStrip2D = nullptr;
        NodeCameraTag *camera = nullptr;
        NodeLightTag *light = nullptr;
        NodePhysicsTag *physics = nullptr;
        NodePhysics2DTag *physics2d = nullptr;
        NodeAudioTag *audio = nullptr;
        NodeSkyboxTag *skybox = nullptr;
        NodeRuntimeUiTag *runtimeUi = nullptr;
        NodePrefabComponent *prefab = nullptr;
        NodeSpriteComponent *sprite = nullptr;
    };
} // namespace pe
