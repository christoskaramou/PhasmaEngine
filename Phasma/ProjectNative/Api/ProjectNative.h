#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#if defined(_WIN32)
#define PHASMA_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#else
#define PHASMA_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace phasma
{
    // Append-only ABI: new ScriptApi function pointers go at the end (bump ScriptAbiVersion); ScriptDesc
    // and ScriptModule layouts are frozen. Modules accept any host ScriptApi with version >= their
    // ScriptAbiVersion and size >= their sizeof(ScriptApi); PhasmaGetScriptModule(hostVersion) returns
    // the module when hostVersion >= its ScriptAbiVersion; hosts accept [ScriptAbiMinVersion, ScriptAbiVersion].
    inline constexpr uint32_t ScriptAbiVersion = 26;
    inline constexpr uint32_t ScriptAbiMinVersion = 5; // v4 modules demand an exact host match
    using Node = uint64_t;
    struct Vec3
    {
        float x, y, z;
    };
    struct Color
    {
        float r, g, b, a;
    };
    enum class UiStyle : uint32_t
    {
        Card,
        Panel,
        Text,
        Button,
        Image
    };
    enum class UiAlignH : uint32_t
    {
        Default,
        Left,
        Center,
        Right
    };
    enum class UiAlignV : uint32_t
    {
        Default,
        Top,
        Middle,
        Bottom
    };
    // Mirrors Lua runtime_ui.set_quad options, defaults included. Frozen layout (v6).
    struct UiQuad
    {
        const char *image = nullptr; // asset-relative path
        const char *label = nullptr, *title = nullptr, *subtitle = nullptr, *body = nullptr, *footer = nullptr;
        Node node = 0; // anchors the quad to a scene node
        float x = 0.0f, y = 0.0f, z = 0.0f, width = 0.0f, height = 0.0f;
        Color fill{0.07f, 0.08f, 0.10f, 0.94f}, border{0.45f, 0.48f, 0.54f, 0.95f}, accent{0.96f, 0.74f, 0.22f, 1.0f};
        Color textColor{0.92f, 0.93f, 0.94f, 1.0f}, imageTint{1.0f, 1.0f, 1.0f, 1.0f}, backgroundImageTint{1.0f, 1.0f, 1.0f, 1.0f};
        float imageWhiten = -1.0f, cornerRadius = -1.0f; // negative keeps the theme default
        float fontScale = 1.0f, offsetX = 0.0f, offsetY = 0.0f, textInsetRight = 0.0f;
        uint32_t radialSegments = 0, radialFilled = 0; // clockwise charge slices, at most 64
        UiStyle style = UiStyle::Card;
        UiAlignH alignH = UiAlignH::Default;
        UiAlignV alignV = UiAlignV::Default;
        bool visible = true, noInput = false, bringToFront = false, fit = false;
        bool selected = false, draggable = false, imageColorize = false, useBackgroundTint = false;
    };
    // Mirrors Lua runtime_ui.get_surface_size.
    struct UiSurface
    {
        uint32_t width, height;
        float uiScale;
        float safeX, safeY, safeWidth, safeHeight;
        uint32_t safeValid;
    };
    // Mirrors Lua runtime_ui.get_state. clicked holds for the frame of the click, on quads too.
    struct UiWidgetState
    {
        bool hovered, active, clicked, rightClicked, down, dragging, dragStarted, dragReleased;
        float mouseX, mouseY, dragDeltaX, dragDeltaY;
    };
    // Lua's node:set_ui for an authored scene UI node (v15): a field applies only when its bit is in NodeUi::set.
    enum NodeUiField : uint32_t
    {
        NodeUiBody = 1u << 0,
        NodeUiTitle = 1u << 1,
        NodeUiSubtitle = 1u << 2,
        NodeUiFooter = 1u << 3,
        NodeUiLabel = 1u << 4,
        NodeUiImage = 1u << 5,
        NodeUiFill = 1u << 6,
        NodeUiBorder = 1u << 7,
        NodeUiAccent = 1u << 8,
        NodeUiTextColor = 1u << 9,
        NodeUiImageTint = 1u << 10,
        NodeUiBackgroundImageTint = 1u << 11,
        NodeUiFontScale = 1u << 12,
        NodeUiImageWhiten = 1u << 13,
        NodeUiAlignH = 1u << 14,
        NodeUiAlignV = 1u << 15,
        NodeUiOffset = 1u << 16,
        NodeUiVisible = 1u << 17,
        NodeUiNoInput = 1u << 18,
        NodeUiUseBackgroundTint = 1u << 19
    };
    struct NodeUi
    {
        uint32_t set = 0;
        const char *body = nullptr, *title = nullptr, *subtitle = nullptr, *footer = nullptr, *label = nullptr,
                   *image = nullptr;
        Color fill{}, border{}, accent{}, textColor{}, imageTint{}, backgroundImageTint{};
        float fontScale = 1.0f, imageWhiten = -1.0f, offsetX = 0.0f, offsetY = 0.0f;
        UiAlignH alignH = UiAlignH::Default;
        UiAlignV alignV = UiAlignV::Default;
        bool visible = true, noInput = false, useBackgroundTint = false;
    };
    // node:get_ui_rect: the drawn rect of the widgets anchored to a node, in surface pixels.
    struct UiRect
    {
        float x, y, width, height;
    };
    // The audio buses of audio.set_*_volume (v13).
    enum class AudioBus : uint32_t
    {
        Master,
        Music,
        Sfx,
        Ambient
    };
    // rhi present modes (v16): Fifo is vsync, Immediate uncapped.
    enum class PresentMode : uint32_t
    {
        Fifo,
        Immediate,
        Mailbox,
        FifoRelaxed
    };
    // engine window modes (v16).
    enum class WindowMode : uint32_t
    {
        Windowed,
        Borderless,
        Fullscreen
    };
    // particles.emit_burst: a field overrides the preset only when its bit is in ParticleBurst::set.
    enum BurstField : uint32_t
    {
        BurstCount = 1u << 0,
        BurstSizeMin = 1u << 1,
        BurstSizeMax = 1u << 2,
        BurstLifeMin = 1u << 3,
        BurstLifeMax = 1u << 4,
        BurstSpawnRadius = 1u << 5,
        BurstNoise = 1u << 6,
        BurstDrag = 1u << 7,
        BurstCleanupDelay = 1u << 8,
        BurstVelocity = 1u << 9,
        BurstGravity = 1u << 10,
        BurstColorStart = 1u << 11,
        BurstColorEnd = 1u << 12,
        BurstStretch = 1u << 13
    };
    // Mirrors particles.emit_burst. preset: hero_take, hero_give, enemy_take or enemy_give (anything else keeps the
    // engine's defaults). v10 prefix is frozen; v24 appends size/stretch.
    struct ParticleBurst
    {
        const char *preset = nullptr;
        Vec3 position{};
        uint32_t set = 0; // BurstField bits
        uint32_t count = 0;
        float sizeMin = 0.0f, sizeMax = 0.0f, lifeMin = 0.0f, lifeMax = 0.0f;
        float spawnRadius = 0.0f, noiseStrength = 0.0f, drag = 0.0f, cleanupDelay = 0.0f;
        Vec3 velocity{}, gravity{};
        Color colorStart{}, colorEnd{};
        uint32_t size = sizeof(ParticleBurst); // v24 extension; read only from v24 modules with BurstStretch set
        float stretch = 0.0f;                  // seconds of velocity, full length clamped to [diameter, 8 * diameter]
    };
    // A fullscreen shader pass over the scene's depth and normals, blended onto the viewport after the given
    // render_graph order. The shader (an asset path) provides mainVS / mainPS and reads the depth at binding 0 and
    // the normals at binding 1. Push constants: vec4 0 = (1/width, 1/height, max(minThickness, thicknessAt1080 *
    // height / 1080), camera near), vec4 1 (offset 4) = params. Frozen layout (v12).
    struct FullscreenPass
    {
        const char *shader = nullptr;
        uint32_t order = 550;
        float thicknessAt1080 = 1.0f, minThickness = 0.0f;
        float params[4] = {};
    };
    enum class ScriptKind : uint32_t
    {
        Global,
        Node
    };
    enum class ScriptMode : uint32_t
    {
        Always,
        Editor,
        Play
    };

    // Main-thread calls only. Handles are validated; no engine objects cross this ABI.
    struct ScriptApi
    {
        uint32_t version;
        uint32_t size;
        void *context;
        void (*log)(void *, const char *) noexcept;
        Node (*findNode)(void *, const char *) noexcept;
        Node (*createCube)(void *, const char *, float) noexcept;
        uint32_t (*isValid)(void *, Node) noexcept;
        // Position/rotation setters on a camera node move the camera itself.
        uint32_t (*getPosition)(void *, Node, Vec3 *) noexcept;
        uint32_t (*setPosition)(void *, Node, Vec3) noexcept;
        // SDL key names (e.g. "W", "Space"); false while UI captures the keyboard.
        uint32_t (*isKeyDown)(void *, const char *) noexcept;
        // Asset-relative prefab path (e.g. "Prefabs/enemy.peprefab"); returns the instance root.
        Node (*instantiatePrefab)(void *, const char *) noexcept;
        // Deletes the node and its subtree.
        uint32_t (*destroyNode)(void *, Node) noexcept;
        uint32_t (*setRotation)(void *, Node, Vec3 eulerDegrees) noexcept;
        uint32_t (*setScale)(void *, Node, Vec3) noexcept;
        // Plays the clip on the node and every animated descendant; false if none has it.
        uint32_t (*playAnimation)(void *, Node, const char *clip, uint32_t loop) noexcept;
        // v6: runtime UI. Screens are created on first use and hidden; Play Stop clears them.
        uint32_t (*showScreen)(void *, const char *screen, uint32_t visible, uint32_t overlay) noexcept;
        uint32_t (*setQuad)(void *, const char *screen, const char *id, const UiQuad *) noexcept;
        uint32_t (*removeWidget)(void *, const char *screen, const char *id) noexcept;
        // False (zeroed) when no runtime UI is active or the surface is not sized yet.
        uint32_t (*getSurfaceSize)(void *, UiSurface *) noexcept;
        // False (zeroed) when the widget does not exist.
        uint32_t (*getWidgetState)(void *, const char *screen, const char *id, UiWidgetState *) noexcept;
        // v7: input and animation timing. Mirrors Lua input.is_left_mouse_down; false while UI captures the mouse.
        uint32_t (*isLeftMouseDown)(void *) noexcept;
        // Animation playback speed on the node and every animated descendant; false if none animates.
        uint32_t (*setAnimationSpeed)(void *, Node, float speed) noexcept;
        // Writes the clip length in seconds into *out; false if node/clip missing or out null.
        uint32_t (*getClipDuration)(void *, Node, const char *clip, float *out) noexcept;
        // Mirrors node:set_visible (render visibility of this node only).
        uint32_t (*setVisible)(void *, Node, uint32_t visible) noexcept;
        // Mirrors animation.get_bone_position: the bone's model-space position from the last posed frame.
        uint32_t (*getBonePosition)(void *, Node, const char *bone, Vec3 *out) noexcept;
        // The root or its first descendant (depth-first) named exactly `name`, as a Lua get_children walk finds it; 0 if none.
        Node (*findChild)(void *, Node root, const char *name) noexcept;
        // v8: primitives and material tint. Mirrors primitives.sphere(radius).
        Node (*createSphere)(void *, const char *name, float radius) noexcept;
        // Mirrors material.set base_color + emissive on the node's first mesh; false if it has no mesh or material.
        uint32_t (*setMaterialColor)(void *, Node, Color baseColor, Vec3 emissive) noexcept;
        // v9: audio and launch options. Mirror audio.play / play_music / stop_music: a bare clip name resolves against
        // the project's Assets/Audio/; false when the build has no audio. Looping per-node sources belong in the scene.
        uint32_t (*playSound)(void *, const char *clip) noexcept;
        uint32_t (*playMusic)(void *, const char *clip) noexcept;
        uint32_t (*stopMusic)(void *) noexcept;
        // Mirrors script.launch_option: copies PE_SCRIPT_<name> into out, NUL-terminated; false if unset, the name is
        // not [A-Z0-9_]{1,64}, or the value does not fit.
        uint32_t (*launchOption)(void *, const char *name, char *out, uint32_t capacity) noexcept;
        // v10: particles and render type. False without a particle system or with a non-finite field.
        uint32_t (*emitBurst)(void *, const ParticleBurst *) noexcept;
        // Mirrors material.set_render_type on the node's first mesh (alpha_blend makes base color alpha count).
        uint32_t (*setRenderType)(void *, Node, const char *type) noexcept;
        // v11: files, as Lua fs.read / fs.write: paths resolve against the project's Assets/ and may not leave it.
        // readFile copies the file when it fits in capacity; *size gets its length whenever it exists.
        uint32_t (*readFile)(void *, const char *path, char *out, uint32_t capacity, uint32_t *size) noexcept;
        uint32_t (*writeFile)(void *, const char *path, const char *data, uint32_t size) noexcept;
        // Mirrors primitives.torus (rings and outlines).
        Node (*createTorus)(void *, const char *name, float majorRadius, float minorRadius, uint32_t majorSegments,
                            uint32_t minorSegments) noexcept;
        // v12: adds (or replaces) the named pass; false for a null/empty name, a missing shader path or a non-finite value.
        uint32_t (*addFullscreenPass)(void *, const char *name, const FullscreenPass *) noexcept;
        uint32_t (*removeFullscreenPass)(void *, const char *name) noexcept;
        // v13: a bus volume, clamped to [0, 1] like the settings sliders; false for an unknown bus, a non-finite
        // value or a build without audio.
        uint32_t (*setVolume)(void *, AudioBus bus, float value) noexcept;
        uint32_t (*getVolume)(void *, AudioBus bus, float *value) noexcept;
        // v14: mirrors runtime_ui.set_style_background, the theme plate every quad of a style draws on
        // ("" restores the default); false for an unknown style or a null path.
        uint32_t (*setStyleBackground)(void *, UiStyle style, const char *image) noexcept;
        // v15: authored scene UI (the __scene_ui widgets a .pescene carries), as Lua drives it. setNodeUi is false
        // for a node without a runtime-UI tag, a set string field that is null or a non-finite number;
        // setNodeEnabled / isNodeEnabled are node:set_enabled / is_enabled (a disabled node hides its UI subtree);
        // getUiRect is false while nothing anchored to the node is drawn.
        uint32_t (*setNodeUi)(void *, Node node, const NodeUi *ui) noexcept;
        uint32_t (*setNodeEnabled)(void *, Node node, uint32_t enabled) noexcept;
        uint32_t (*isNodeEnabled)(void *, Node node) noexcept;
        uint32_t (*getUiRect)(void *, Node node, UiRect *rect) noexcept;
        uint32_t (*getScale)(void *, Node node, Vec3 *scale) noexcept;
        // v16: what a settings screen and a quit button reach, as Lua does. getSetting / setSetting are
        // settings.get / set on the bool and number keys (a bool as 0 or 1; false for other keys or a non-finite
        // value). setPresentMode is rhi.change_present_mode and reports the mode in effect (a surface may lack
        // the one asked). setWindowMode is engine.set_window_mode, false in the editor. getTextScale /
        // setTextScale are runtime_ui's text scale (clamped to [0.5, 3]). getFrameSeconds is the frame's
        // unscaled time (engine.get_metrics; update's dt follows time_scale). loadScene is scene.load(name),
        // applied after this frame's C++ updates. quit is a quit button: leave play mode, else quit.
        uint32_t (*getSetting)(void *, const char *name, double *value) noexcept;
        uint32_t (*setSetting)(void *, const char *name, double value) noexcept;
        uint32_t (*getPresentMode)(void *, PresentMode *mode) noexcept;
        uint32_t (*setPresentMode)(void *, PresentMode mode, PresentMode *applied) noexcept;
        uint32_t (*getWindowMode)(void *, WindowMode *mode) noexcept;
        uint32_t (*setWindowMode)(void *, WindowMode mode) noexcept;
        uint32_t (*getTextScale)(void *, float *scale) noexcept;
        uint32_t (*setTextScale)(void *, float scale) noexcept;
        uint32_t (*getFrameSeconds)(void *, double *seconds) noexcept;
        uint32_t (*loadScene)(void *, const char *name) noexcept;
        uint32_t (*quit)(void *) noexcept;
        // v17: input.get_mouse_wheel, this frame's wheel steps (y up is positive).
        uint32_t (*getMouseWheel)(void *, float *x, float *y) noexcept;
        // v18: input.get_mouse_position (window pixels, 0, 0 while the runtime UI holds the mouse) and
        // engine.get_window_size, which scale a pointer into runtime UI surface pixels.
        uint32_t (*getMousePosition)(void *, float *x, float *y) noexcept;
        uint32_t (*getWindowSize)(void *, uint32_t *width, uint32_t *height) noexcept;
        // v19: animation layers and light intensity. Mirror animation.play_layer (a rig-space override layer on
        // the named bones, `mask` separated by ',' and never empty), set_layer_speed and stop_layer on the node
        // and every animated descendant (play only where the clip exists); get_layer_state (seconds) and
        // get_markers read the first animated node of the tree. setLightIntensity is lights.set_property
        // "intensity" on every point, spot and area light the node or a descendant owns.
        uint32_t (*playAnimationLayer)(void *, Node, const char *clip, const char *mask, uint32_t loop,
                                       float speed) noexcept;
        uint32_t (*setAnimationLayerSpeed)(void *, Node, float speed) noexcept;
        uint32_t (*stopAnimationLayer)(void *, Node) noexcept;
        uint32_t (*getAnimationLayer)(void *, Node, uint32_t *active, float *seconds, float *duration) noexcept;
        uint32_t (*getClipMarker)(void *, Node, const char *clip, const char *marker, float *seconds) noexcept;
        uint32_t (*setLightIntensity)(void *, Node, float intensity) noexcept;
        // v20: input.vibrate, a haptic pulse of ms milliseconds (12 when ms <= 0); 0 without a vibrator (desktop).
        uint32_t (*vibrate)(void *, int32_t ms) noexcept;
        // v21: textured sprites. createQuad mirrors primitives.quad; setMaterialTexture is material.set_texture
        // (slot "base_color", "emissive", "normal", ...; the path asset-relative), setDoubleSided
        // material.set_double_sided and setAlphaCutoff material.set(node, "alpha_cutoff", value), on the node's mesh.
        Node (*createQuad)(void *, const char *name, float width, float height) noexcept;
        uint32_t (*setMaterialTexture)(void *, Node, const char *slot, const char *path) noexcept;
        uint32_t (*setDoubleSided)(void *, Node, uint32_t doubleSided) noexcept;
        uint32_t (*setAlphaCutoff)(void *, Node, float cutoff) noexcept;
        // v22: playAnimationLayer with animation.play_layer's anchor: the masked bones are aligned to that bone of
        // the base pose, so an attacking upper body rides a running pelvis instead of the attack clip's own hips.
        // 0 when a node's rig lacks the anchor.
        uint32_t (*playAnimationLayerAnchored)(void *, Node, const char *clip, const char *mask, uint32_t loop,
                                               float speed, const char *anchor) noexcept;
        // v23: the fingers on the screen, for on-screen sticks: the index-th finger down (at most two; a finger that
        // landed on the runtime UI is the UI's), its id and window-pixel position. 0 past the last (the desktop has none).
        uint32_t (*getTouch)(void *, uint32_t index, int64_t *id, float *x, float *y) noexcept;
        // v24: pose crossfades (0 seconds hard-cuts), including an optional anchored attack layer.
        uint32_t (*playAnimationBlend)(void *, Node, const char *clip, uint32_t loop, float fadeSeconds) noexcept;
        uint32_t (*playAnimationLayerBlend)(void *, Node, const char *clip, const char *mask, uint32_t loop,
                                            float speed, const char *anchor, float fadeSeconds) noexcept;
        uint32_t (*playSoundEx)(void *, const char *clip, float volume, float pitch) noexcept;
        // v25: named profiler scopes around game code (PhasmaProfiler totals, spikes, the Advisor).
        void (*profileBegin)(void *, const char *name) noexcept;
        void (*profileEnd)(void *) noexcept;
        // v26: position and Euler rotation (degrees) in one call: what SetPosition then SetRotation leave, with one
        // node lookup and one transform update (a crowd moves every member every frame).
        uint32_t (*setTransform)(void *, Node node, Vec3 position, Vec3 eulerDegrees) noexcept;
    };
    struct ScriptDesc
    {
        const char *name;
        ScriptKind kind;
        ScriptMode mode; // Node scripts use the node's run mode.
        uint32_t (*create)(const ScriptApi *, Node, void **) noexcept;
        uint32_t (*update)(void *, double) noexcept;
        void (*destroy)(void *) noexcept;
        const char *sourceFile = nullptr;
    };
    struct ScriptModule
    {
        uint32_t version;
        uint32_t size;
        uint32_t scriptCount;
        const ScriptDesc *scripts;
    };
    using GetScriptModule = const ScriptModule *(*)(uint32_t) noexcept;

    // Instances are allocated and destroyed inside the module. Destructors must not throw.
    template <class T>
    ScriptDesc Script(const char *name, ScriptKind kind, ScriptMode mode = ScriptMode::Play)
    {
        static_assert(std::is_nothrow_destructible_v<T>, "Script destructors must not throw");
        return {name, kind, mode,
                [](const ScriptApi *api, Node node, void **state) noexcept -> uint32_t
                {
                    if (!state)
                        return 0;
                    *state = nullptr;
                    if (!api || api->version < ScriptAbiVersion || api->size < sizeof(ScriptApi))
                        return 0;
                    try
                    {
                        *state = new T(*api, node);
                        return 1;
                    }
                    catch (...)
                    {
                        *state = nullptr;
                        return 0;
                    }
                },
                [](void *state, double dt) noexcept -> uint32_t
                {
                    try
                    {
                        static_cast<T *>(state)->Update(dt);
                        return 1;
                    }
                    catch (...)
                    {
                        return 0;
                    }
                },
                [](void *state) noexcept
                { delete static_cast<T *>(state); }};
    }

    class World
    {
    public:
        explicit World(const ScriptApi &api) : m_api(api) {}
        void Log(const char *message) const { m_api.log(m_api.context, message); }
        Node Find(const char *name) const { return m_api.findNode(m_api.context, name); }
        Node CreateCube(const char *name, float size) const { return m_api.createCube(m_api.context, name, size); }
        bool Valid(Node node) const { return m_api.isValid(m_api.context, node) != 0; }
        bool Position(Node node, Vec3 &value) const { return m_api.getPosition(m_api.context, node, &value) != 0; }
        bool SetPosition(Node node, Vec3 value) const { return m_api.setPosition(m_api.context, node, value) != 0; }
        bool KeyDown(const char *name) const { return m_api.isKeyDown(m_api.context, name) != 0; }
        Node Instantiate(const char *prefab) const { return m_api.instantiatePrefab(m_api.context, prefab); }
        bool Destroy(Node node) const { return m_api.destroyNode(m_api.context, node) != 0; }
        bool SetRotation(Node node, Vec3 eulerDegrees) const { return m_api.setRotation(m_api.context, node, eulerDegrees) != 0; }
        bool SetTransform(Node node, Vec3 position, Vec3 eulerDegrees) const
        {
            return m_api.setTransform(m_api.context, node, position, eulerDegrees) != 0;
        }
        bool SetScale(Node node, Vec3 value) const { return m_api.setScale(m_api.context, node, value) != 0; }
        bool Play(Node node, const char *clip, bool loop = true) const { return m_api.playAnimation(m_api.context, node, clip, loop) != 0; }
        bool ShowScreen(const char *screen, bool visible = true, bool overlay = true) const { return m_api.showScreen(m_api.context, screen, visible, overlay) != 0; }
        bool SetQuad(const char *screen, const char *id, const UiQuad &quad) const { return m_api.setQuad(m_api.context, screen, id, &quad) != 0; }
        bool RemoveWidget(const char *screen, const char *id) const { return m_api.removeWidget(m_api.context, screen, id) != 0; }
        bool SurfaceSize(UiSurface &surface) const { return m_api.getSurfaceSize(m_api.context, &surface) != 0; }
        bool WidgetState(const char *screen, const char *id, UiWidgetState &state) const { return m_api.getWidgetState(m_api.context, screen, id, &state) != 0; }
        bool LeftMouseDown() const { return m_api.isLeftMouseDown(m_api.context) != 0; }
        bool SetSpeed(Node node, float speed) const { return m_api.setAnimationSpeed(m_api.context, node, speed) != 0; }
        bool GetClipDuration(Node node, const char *clip, float &seconds) const { return m_api.getClipDuration(m_api.context, node, clip, &seconds) != 0; }
        bool SetVisible(Node node, bool visible) const { return m_api.setVisible(m_api.context, node, visible) != 0; }
        bool BonePosition(Node node, const char *bone, Vec3 &position) const { return m_api.getBonePosition(m_api.context, node, bone, &position) != 0; }
        Node FindChild(Node root, const char *name) const { return m_api.findChild(m_api.context, root, name); }
        Node CreateSphere(const char *name, float radius) const { return m_api.createSphere(m_api.context, name, radius); }
        bool SetColor(Node node, Color baseColor, Vec3 emissive) const { return m_api.setMaterialColor(m_api.context, node, baseColor, emissive) != 0; }
        bool PlaySound(const char *clip) const { return m_api.playSound(m_api.context, clip) != 0; }
        bool PlayMusic(const char *clip) const { return m_api.playMusic(m_api.context, clip) != 0; }
        bool StopMusic() const { return m_api.stopMusic(m_api.context) != 0; }
        bool Burst(const ParticleBurst &burst) const { return m_api.emitBurst(m_api.context, &burst) != 0; }
        bool ReadFile(const char *path, std::string &out) const
        {
            uint32_t size = 0;
            out.resize(256);
            if (m_api.readFile(m_api.context, path, out.data(), static_cast<uint32_t>(out.size()), &size) == 0)
            {
                // A failed read leaves out empty: a caller that ignores the result must not see the probe buffer.
                if (size <= out.size())
                {
                    out.clear();
                    return false; // missing, outside Assets, or empty
                }
                out.resize(size);
                if (m_api.readFile(m_api.context, path, out.data(), size, &size) == 0)
                {
                    out.clear();
                    return false;
                }
            }
            out.resize(size);
            return true;
        }
        Node CreateTorus(const char *name, float majorRadius, float minorRadius, uint32_t majorSegments = 64,
                         uint32_t minorSegments = 16) const
        {
            return m_api.createTorus(m_api.context, name, majorRadius, minorRadius, majorSegments, minorSegments);
        }
        bool WriteFile(const char *path, const std::string &data) const
        {
            return m_api.writeFile(m_api.context, path, data.data(), static_cast<uint32_t>(data.size())) != 0;
        }
        bool SetRenderType(Node node, const char *type) const { return m_api.setRenderType(m_api.context, node, type) != 0; }
        bool AddFullscreenPass(const char *name, const FullscreenPass &pass) const { return m_api.addFullscreenPass(m_api.context, name, &pass) != 0; }
        bool RemoveFullscreenPass(const char *name) const { return m_api.removeFullscreenPass(m_api.context, name) != 0; }
        bool SetVolume(AudioBus bus, float value) const { return m_api.setVolume(m_api.context, bus, value) != 0; }
        bool GetVolume(AudioBus bus, float &value) const { return m_api.getVolume(m_api.context, bus, &value) != 0; }
        bool SetStyleBackground(UiStyle style, const char *image) const
        {
            return m_api.setStyleBackground(m_api.context, style, image) != 0;
        }
        bool SetNodeUi(Node node, const NodeUi &ui) const { return m_api.setNodeUi(m_api.context, node, &ui) != 0; }
        bool SetEnabled(Node node, bool enabled) const { return m_api.setNodeEnabled(m_api.context, node, enabled) != 0; }
        bool IsEnabled(Node node) const { return m_api.isNodeEnabled(m_api.context, node) != 0; }
        bool GetUiRect(Node node, UiRect &rect) const { return m_api.getUiRect(m_api.context, node, &rect) != 0; }
        bool GetScale(Node node, Vec3 &scale) const { return m_api.getScale(m_api.context, node, &scale) != 0; }
        bool GetSetting(const char *name, double &value) const { return m_api.getSetting(m_api.context, name, &value) != 0; }
        bool SetSetting(const char *name, double value) const { return m_api.setSetting(m_api.context, name, value) != 0; }
        bool GetPresentMode(PresentMode &mode) const { return m_api.getPresentMode(m_api.context, &mode) != 0; }
        bool SetPresentMode(PresentMode mode, PresentMode &applied) const
        {
            return m_api.setPresentMode(m_api.context, mode, &applied) != 0;
        }
        bool GetWindowMode(WindowMode &mode) const { return m_api.getWindowMode(m_api.context, &mode) != 0; }
        bool SetWindowMode(WindowMode mode) const { return m_api.setWindowMode(m_api.context, mode) != 0; }
        bool GetTextScale(float &scale) const { return m_api.getTextScale(m_api.context, &scale) != 0; }
        bool SetTextScale(float scale) const { return m_api.setTextScale(m_api.context, scale) != 0; }
        bool FrameSeconds(double &seconds) const { return m_api.getFrameSeconds(m_api.context, &seconds) != 0; }
        bool LoadScene(const char *name) const { return m_api.loadScene(m_api.context, name) != 0; }
        bool Quit() const { return m_api.quit(m_api.context) != 0; }
        bool MouseWheel(float &x, float &y) const { return m_api.getMouseWheel(m_api.context, &x, &y) != 0; }
        bool MousePosition(float &x, float &y) const { return m_api.getMousePosition(m_api.context, &x, &y) != 0; }
        bool WindowSize(uint32_t &width, uint32_t &height) const
        {
            return m_api.getWindowSize(m_api.context, &width, &height) != 0;
        }
        bool PlayLayer(Node node, const char *clip, const char *mask, bool loop = true, float speed = 1.0f,
                       const char *anchor = nullptr) const
        {
            return (anchor && *anchor ? m_api.playAnimationLayerAnchored(m_api.context, node, clip, mask, loop, speed, anchor)
                                      : m_api.playAnimationLayer(m_api.context, node, clip, mask, loop, speed)) != 0;
        }
        bool SetLayerSpeed(Node node, float speed) const
        {
            return m_api.setAnimationLayerSpeed(m_api.context, node, speed) != 0;
        }
        bool StopLayer(Node node) const { return m_api.stopAnimationLayer(m_api.context, node) != 0; }
        // false when nothing in the tree animates; `active` false when no layer plays.
        bool LayerState(Node node, bool &active, float &seconds, float &duration) const
        {
            uint32_t on = 0;
            const bool ok = m_api.getAnimationLayer(m_api.context, node, &on, &seconds, &duration) != 0;
            active = on != 0;
            return ok;
        }
        // A named marker's time in seconds (the name compared without case); false if none.
        bool ClipMarker(Node node, const char *clip, const char *marker, float &seconds) const
        {
            return m_api.getClipMarker(m_api.context, node, clip, marker, &seconds) != 0;
        }
        bool SetLightIntensity(Node node, float intensity) const
        {
            return m_api.setLightIntensity(m_api.context, node, intensity) != 0;
        }
        bool Vibrate(int32_t ms) const { return m_api.vibrate(m_api.context, ms) != 0; }
        bool Touch(uint32_t index, int64_t &id, float &x, float &y) const
        {
            return m_api.getTouch(m_api.context, index, &id, &x, &y) != 0;
        }
        Node CreateQuad(const char *name, float width, float height) const
        {
            return m_api.createQuad(m_api.context, name, width, height);
        }
        bool SetTexture(Node node, const char *slot, const char *path) const
        {
            return m_api.setMaterialTexture(m_api.context, node, slot, path) != 0;
        }
        bool SetDoubleSided(Node node, bool doubleSided) const
        {
            return m_api.setDoubleSided(m_api.context, node, doubleSided) != 0;
        }
        bool SetAlphaCutoff(Node node, float cutoff) const { return m_api.setAlphaCutoff(m_api.context, node, cutoff) != 0; }
        template <uint32_t N>
        bool LaunchOption(const char *name, char (&out)[N]) const
        {
            return m_api.launchOption(m_api.context, name, out, N) != 0;
        }
        bool Clicked(const char *screen, const char *id) const
        {
            UiWidgetState state{};
            return WidgetState(screen, id, state) && state.clicked;
        }

        bool PlayBlend(Node node, const char *clip, bool loop = true, float fadeSeconds = 0.1f) const
        {
            return m_api.playAnimationBlend(m_api.context, node, clip, loop, fadeSeconds) != 0;
        }
        bool PlayLayerBlend(Node node, const char *clip, const char *mask, bool loop = true, float speed = 1.0f,
                            const char *anchor = nullptr, float fadeSeconds = 0.1f) const
        {
            return m_api.playAnimationLayerBlend(m_api.context, node, clip, mask, loop, speed, anchor, fadeSeconds) != 0;
        }
        bool PlaySoundEx(const char *clip, float volume, float pitch) const
        {
            return m_api.playSoundEx(m_api.context, clip, volume, pitch) != 0;
        }
        // Pair each Begin with an End (ProfileScope does); the host closes any left open after the update.
        void ProfileBegin(const char *name) const { m_api.profileBegin(m_api.context, name); }
        void ProfileEnd() const { m_api.profileEnd(m_api.context); }

    private:
        const ScriptApi &m_api;
    };

    // A profiler scope for the rest of a block: ProfileScope scope(world, "Creep View");
    struct ProfileScope
    {
        ProfileScope(const World &world, const char *name) : m_world(world) { m_world.ProfileBegin(name); }
        ~ProfileScope() { m_world.ProfileEnd(); }
        ProfileScope(const ProfileScope &) = delete;
        ProfileScope &operator=(const ProfileScope &) = delete;

    private:
        const World &m_world;
    };
} // namespace phasma
