#pragma once

#include "Audio/AudioTypes.h"     // AudioSourceDesc (value member of NodeTriggerZoneTag)
#include "Physics/PhysicsTypes.h" // PhysicsBodyType (zone physics section)

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

    // Where a plain Component_Script runs its init/update/destroy lifecycle:
    // Player (default) = play mode + the player; Editor = the editor while editing
    // (not in play); Both = everywhere. Independent of the legacy update_editor() hook.
    enum class ScriptRunMode : uint8_t
    {
        Player = 0,
        Editor = 1,
        Both = 2,
    };

    class NodeScriptComponent : public IComponent
    {
    public:
        std::string path;
        ScriptRunMode runMode = ScriptRunMode::Player;
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

    // Zone bounding shape, shared by every section: drives the distance falloff (Scene::VolumeDistanceOutside),
    // the viewport gizmo, and the shape the Physics section's collider inherits.
    enum class ZoneShape : uint8_t
    {
        Box = 0,
        Sphere = 1
    };

    // Physics section role: Sensor = pass-through trigger that fires the zone's physics script; Solid =
    // a real collider that blocks bodies (no script).
    enum class ZonePhysicsMode : uint8_t
    {
        Sensor = 0,
        Solid = 1
    };

    // Which physics world the Physics section uses (they don't mix): Physics3D = Jolt, Physics2D = Box2D.
    enum class ZonePhysicsEngine : uint8_t
    {
        Physics3D = 0,
        Physics2D = 1
    };

    // Unified "Trigger Zone": one bounded box (the node transform) that drives several scene systems.
    // The common fields feed the shared box-distance falloff (Scene::VolumeDistanceOutside); each
    // feature is an opt-in section toggled on per zone:
    //   - Script:       call Lua on_enter/on_exit on the node's own script when the camera crosses in/out.
    //   - Post Process: make `postProcess` the active profile (blended over the scene default) while inside.
    //   - Audio:        scale AudioSystem master/music/sfx while inside.
    //   - Physics:      register the box/sphere as a Jolt sensor or solid collider (during play); a body
    //                   overlapping a sensor fires the Physics section's OWN script (separate from Script).
    // More sections land later. wasInside is per-frame transition state (runtime-only).
    class NodeTriggerZoneTag : public IComponent
    {
    public:
        // --- common (always present) ---
        float priority = 0.0f;       // higher wins when zones overlap (post-process / audio)
        float blend = 1.0f;          // 0..1 master weight for this zone's blended effects
        float blend_distance = 0.0f; // world-unit fade OUTSIDE the box (full at the wall); 0 = hard edge
        TriggerRunMode runMode = TriggerRunMode::Both;
        ZoneShape shape = ZoneShape::Box; // bounds shape: feeds falloff + gizmo + physics collider

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

        // --- Physics section ---
        // While simulating, the zone registers a Jolt body (the node's physics body) shaped to the common
        // `shape`. Sensor mode = pass-through trigger that fires this section's OWN script (separate from
        // the Script section, never mixed); Solid mode = a collider that blocks bodies. ponytail: a
        // Physics-enabled zone owns the node's single physics body — don't also add a separate one.
        bool physicsEnabled = false;
        ZonePhysicsEngine physicsEngine = ZonePhysicsEngine::Physics3D; // Jolt (3D) or Box2D (2D)
        ZonePhysicsMode physicsMode = ZonePhysicsMode::Sensor;
        PhysicsBodyType physicsBodyType = PhysicsBodyType::Static; // mapped to the 2D enum when engine == 2D
        float physicsMass = 1.0f;                                  // Solid/Dynamic only
        float physicsFriction = 0.5f;                              // Solid only
        float physicsRestitution = 0.3f;                           // Solid only (bounciness)
        std::string physicsFilterTag;                              // Sensor: only fire for bodies whose node name contains this (empty = any)
        std::string physicsScriptPath;                             // the Physics section's OWN .lua (separate from scriptPath)
        std::string physicsOnEnter = "on_enter";
        std::string physicsOnExit = "on_exit";
        bool physicsBodyActive = false; // runtime-only: this zone created+owns the body right now
        // Force field (Sensor only): while a body overlaps the sensor, push it every frame by `physicsForce`
        // (world-space). Layered on the sensor as an option — no new mode. Bodies tracked live by the
        // sensor enter/exit callbacks; pushed in Scene::UpdatePhysicsZones.
        bool physicsForceField = false;
        vec3 physicsForce = vec3(0.0f, 20.0f, 0.0f);
        std::vector<NodeId *> physicsBodiesInside; // runtime-only: bodies currently overlapping the sensor

        // --- Spawn section ---
        // Instantiate a prefab at the zone when the camera enters; remove it on exit (toggle). Re-entry
        // only re-spawns once the previous instance is gone, so despawnOnExit=false keeps a single copy.
        bool spawnEnabled = false;
        std::string spawnPrefabPath; // .peprefab (project-relative or absolute)
        bool spawnDespawnOnExit = true;
        NodeId *spawnedNode = nullptr; // runtime-only: last spawned instance (nullptr/dead = none)

        // --- Streaming section ---
        // Enable a named target subtree while inside; disable it outside. Author heavy region content
        // disabled, then light it up only when the camera is near (LOD / region streaming toggle).
        bool streamEnabled = false;
        std::string streamTargetName;

        // --- Camera section ---
        // While inside: activate a named camera node and/or override the active camera's horizontal FOV;
        // restores the previous camera + FOV on exit.
        bool cameraEnabled = false;
        std::string cameraTargetName;  // camera node to activate (empty = keep current camera)
        float cameraFovDeg = 0.0f;     // >0: override horizontal FOV (degrees) while inside; 0 = no change
        Camera *cameraPrev = nullptr;  // runtime-only: camera active before we switched (restore target)
        float cameraPrevFovDeg = 0.0f; // runtime-only: FOV before our override
    };

    // Singleton "Voxel World" authoring node: all voxel-world settings live here and VoxelSystem
    // reconciles the live world against it every frame (create on enable, recreate on config change,
    // destroy on disable/delete). The node's world position is the volume center for bounded
    // (worldRadius > 0) and non-streaming worlds. Section size (16) and block size (1 unit) are
    // engine constants baked into the packed vertex format — not per-world settings.
    class NodeVoxelWorldTag : public IComponent
    {
    public:
        bool worldEnabled = true;        // build/keep the world while the component is enabled
        bool streaming = true;           // stream columns around the anchor; off = fixed grid at the node
        bool anchorFollowsCamera = true; // anchor = active camera; off = scripts drive voxel.set_anchor
        int loadRadius = 8;              // streaming radius, columns
        int unloadMargin = 2;            // extra columns kept loaded past loadRadius
        int uploadBudget = 4;            // section mesh uploads applied per frame
        int groundY = 64;                // terrain base height fed to the generator
        int worldRadius = 0;             // total world bound in columns around the node (0 = infinite)
        bool lodEnabled = false;         // distance LOD (coarser mesh per lod0Radius band)
        int lod0Radius = 5;              // full-detail radius in columns when LOD is on
        std::string saveDir;             // .pevcol persistence dir under Assets ("" = no persistence)
        bool autoRebuild = true;         // worldgen edits apply on their own (debounced); off = only via "Rebuild World"
        // Worldgen (mirrors VoxelConfig; see MapGen.h for the heightmap/strata model):
        float noiseAmplitude = 28.0f;    // peak height above groundY in blocks; 0 = flat plain
        float noiseFeatureScale = 96.0f; // feature wavelength in blocks (bigger = rolling hills)
        int noiseSeed = 0;               // each seed is a different world
        bool caves = true;
        int seaLevel = -1;         // <0 = auto (groundY - 2), 0 = no water, >0 = absolute blocks
        std::string heightmapPath; // grayscale surface map under Assets; empty = noise terrain
        // Heightmap value 0..1 -> groundHeight + lerp(heightMin, heightMax, value) in blocks (MapGen::MapHeight).
        float heightMin = -32.0f;
        float heightMax = 32.0f;
        float groundHeight = 0.0f;
        std::string strata1Path;   // thickness map of the band under the surface block
        std::string strata2Path;   // thickness map of the band under strata 1
        std::string featuresPath;  // decoration map: 1=tree 2=rock 3=road 4=olive 5=cypress at that pixel
        int blocksPerPixel = 1;    // one map pixel spans this many blocks in X/Z
        int surfaceBlock = 3;      // block ids: 1=stone 2=dirt 3=grass 4=water, 0=air
        bool surfaceBands = false; // top block by elevation (sand/dry_grass/rock/snow) instead of surfaceBlock
        int strata1Block = 2;
        int strata2Block = 1;
        int fillBlock = 1;        // below the strata to y=0; 0 = air (floating shells)
        int strata1Thickness = 3; // fixed thickness when the strata map is absent
        int strata2Thickness = 8;
        bool rebuildRequested = false; // transient: inspector "Rebuild" (repainted map, same path)
    };

    // Singleton streamed-isosurface-terrain node (TerrainSystem reconciles it). Worldgen reuses the
    // shared generator seam (a grayscale heightmap when heightmapPath is set, else noise); 3D shape
    // comes from `overhangs` (noise relief) and CSG sculpting. See TerrainWorld.h.
    class NodeTerrainTag : public IComponent
    {
    public:
        bool worldEnabled = true;  // build/keep the terrain while the component is enabled
        int sizeXMeters = 256;     // terrain width (m); 0 with a map = the map's X extent
        int sizeZMeters = 256;     // terrain depth (m); 0 with a map = the map's Z extent
        float groundHeight = 0.0f; // datum (m) the mid-gray height level sits at
        float heightMin = -32.0f;  // height offset (m) at map value 0 (black), relative to groundHeight
        float heightMax = 32.0f;   // height offset (m) at map value 1 (white), relative to groundHeight
        float seaLevelM = 0.0f;    // water height (m): surface below this is tinted underwater
        std::string heightmapPath; // grayscale surface map under Assets; empty = noise terrain
        // Grayscale cave map (paint it in Map Painter): pixel value = cave openness under the surface
        // (0 = solid); voids pinch closed where the paint fades. Works on noise AND heightmap terrain.
        std::string cavesPath;
        float noiseFeatureScale = 96.0f;
        int noiseSeed = 0;
        float metersPerPixel = 1.0f;     // world metres each heightmap pixel / mesh cell spans in X/Z
        bool physics = false;            // per-tile static Jolt triangle-mesh colliders (live toggle)
        float physicsFriction = 0.5f;    // collider surface friction (live)
        float physicsRestitution = 0.3f; // collider bounciness (live)
        bool streaming = false;          // windows follow the camera; adds coarse view rings + skirts
        float overhangs = 0.0f;          // 0..1 worldgen 3D relief: cliffs undercut, hollows open (noise terrain)
        float collisionRadiusM = 0.0f;   // colliders within this range of the camera (0 = all tiles; live)
        bool autoRebuild = true;         // worldgen edits apply on their own (debounced); off = only via "Rebuild"
        bool rebuildRequested = false;   // transient: inspector "Rebuild" / Map Painter save (repainted map, same path)
        // Painted mesh scatter: map pixel value = 1-based index into scatterMeshes (builtin
        // "tree"/"rock"/"grass" or a model asset path); instances bake into the terrain tiles.
        std::string scatterPath;
        std::vector<std::string> scatterMeshes;
        // Triplanar texture splatting: 4 layer albedo textures (0 grass, 1 rock, 2 sand, 3 snow) and
        // an optional RGBA splat map (layer weights). Empty splat = auto height/slope selection.
        std::string splatPath;
        std::array<std::string, 4> layerPaths = {"Textures/Voxel/grass.png", "Textures/Voxel/rock.png",
                                                 "Textures/Voxel/sand.png", "Textures/Voxel/snow.png"};
        // Per-layer material maps (RGB = tangent normal, A = roughness); ship defaults give lit terrain
        // micro-relief instead of flat tint. Clear a path for a flat 1x1 (geometric normal, as before).
        std::array<std::string, 4> materialPaths = {"Textures/Voxel/grass_n.png", "Textures/Voxel/rock_n.png",
                                                    "Textures/Voxel/sand_n.png", "Textures/Voxel/snow_n.png"};
        float textureScaleM = 3.0f; // metres per triplanar texture tile (live, no rebuild)
        // Persistent brush strokes, flat 7-float records [type, cx, cy, cz, radius, a, b]: type 0 =
        // CSG sphere (a = 1 digs), type 1 = level toward y = a with weight b. Serialized with the
        // scene; TerrainSystem syncs live-world ops back here and seeds recreated worlds from it.
        std::vector<float> terrainOps;
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
        NodeVoxelWorldTag *voxelWorld = nullptr;
        NodeTerrainTag *terrain = nullptr;
    };
} // namespace pe
