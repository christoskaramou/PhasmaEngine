#pragma once

#include "Base/Settings.h"    // PostProcessProfile (value member of NodeTriggerZoneTag)
#include "Audio/AudioTypes.h" // AudioSourceDesc (value member of NodeTriggerZoneTag)

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

    // Marker for the singleton "Scene Settings" anchor node. Carries no data — its inspector edits
    // the global SceneSettings singleton (which is what gets serialized in the scene settings block).
    class NodeSceneSettingsTag : public IComponent
    {
    };

    // Where a trigger zone's effects/script apply. Editor = only while editing (not in play / not the
    // standalone player); Player = only in play mode or the standalone player; Both = everywhere.
    enum class TriggerRunMode : uint8_t
    {
        Editor = 0,
        Player = 1,
        Both = 2
    };

    // Unified "Trigger Zone": one bounded box (the node transform) that drives several scene systems.
    // The common fields feed the shared box-distance falloff (Scene::VolumeDistanceOutside); each
    // feature is an opt-in section toggled on per zone:
    //   - Script:       call Lua on_enter/on_exit on the node's own script when the camera crosses in/out.
    //   - Post Process: make `postProcess` the active profile (blended over the scene default) while inside.
    //   - Audio:        scale AudioSystem master/music/sfx while inside.
    // More sections (physics, etc.) land later. wasInside is per-frame transition state (runtime-only).
    class NodeTriggerZoneTag : public IComponent
    {
    public:
        // --- common (always present) ---
        float priority = 0.0f;       // higher wins when zones overlap (post-process / audio)
        float blend = 1.0f;          // 0..1 master weight for this zone's blended effects
        float blend_distance = 0.0f; // world-unit fade OUTSIDE the box (full at the wall); 0 = hard edge
        TriggerRunMode runMode = TriggerRunMode::Both;

        // --- Script section ---
        // The zone owns its OWN script (scriptPath, separate from the node's plain Component_Script).
        // On enter/exit it calls onEnter/onExit in that script's isolated environment.
        bool scriptEnabled = false;
        std::string scriptPath;           // the zone's own .lua (project-relative or absolute)
        std::string onEnter = "on_enter"; // function name in the zone script env
        std::string onExit = "on_exit";
        bool fireForCamera = true; // ponytail: camera-only for now; add tracked-node targets if needed
        bool wasInside = false;    // runtime transition state

        // --- Post Process section ---
        bool postProcessEnabled = false;
        PostProcessProfile postProcess;

        // --- Audio section ---
        // The zone owns its OWN audio source (separate from the node's plain Component_Audio): plays it
        // while the camera is inside, blending volume in over blend_distance (0 at the outer edge of the
        // band -> blend fully inside); stops it outside. pitch/loop/spatial are honored as authored.
        bool audioEnabled = false;
        AudioSourceDesc audioSource;
    };

    enum class NodeRuntimeUiWidgetType : uint8_t
    {
        Panel,
        Text,
        Button,
        Image
    };

    class NodeRuntimeUiTag : public IComponent
    {
    public:
        NodeRuntimeUiWidgetType widgetType = NodeRuntimeUiWidgetType::Panel;
        std::string screenId = "__scene_ui";
        std::string widgetId;
        std::string label;
        std::string title;
        std::string subtitle;
        std::string body;
        std::string footer;
        std::string imagePath;
        std::string actionName = "click";
        std::string actionFunction;
        vec4 fillColor = vec4(0.07f, 0.08f, 0.10f, 0.94f);
        vec4 borderColor = vec4(0.45f, 0.48f, 0.54f, 0.95f);
        vec4 accentColor = vec4(0.96f, 0.74f, 0.22f, 1.0f);
        vec4 textColor = vec4(0.92f, 0.93f, 0.94f, 1.0f);
        vec4 imageTint = vec4(1.0f);
        // RectTransform-style layout. anchor = screen point the element sticks to
        // (0..1 of the surface; (0,0)=top-left, (1,1)=bottom-right) for resolution
        // independence. pivot = the element's own reference point (0..1); the node
        // translation places the pivot, and the rect is laid out around it. Screen
        // rect: topLeft = anchor*surface + translation - pivot*size.
        vec2 anchor = vec2(0.0f, 0.0f);
        vec2 pivot = vec2(0.5f, 0.5f);
        float fontScale = 1.0f;
        // Text alignment inside the widget rect (title/body/label text + button
        // captions). 0 = default (text -> top-left, button -> center/middle),
        // 1 = left/top, 2 = center/middle, 3 = right/bottom. textOffset adds a pixel
        // nudge (x,y) after alignment.
        uint8_t textAlignH = 0;
        uint8_t textAlignV = 0;
        vec2 textOffset = vec2(0.0f);
        bool authored = false;
        bool visible = true;
        bool draggable = false;
        bool noInput = false;
        bool bringToFront = false;
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
        NodeSceneSettingsTag *sceneSettings = nullptr;
        NodeRuntimeUiTag *runtimeUi = nullptr;
        NodePrefabComponent *prefab = nullptr;
        NodeSpriteComponent *sprite = nullptr;
        NodeTriggerZoneTag *triggerZone = nullptr;
    };
} // namespace pe
