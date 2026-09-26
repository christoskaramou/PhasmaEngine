#pragma once
#include <cstdint>
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
    inline constexpr uint32_t ScriptAbiVersion = 6;
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
        bool SetScale(Node node, Vec3 value) const { return m_api.setScale(m_api.context, node, value) != 0; }
        bool Play(Node node, const char *clip, bool loop = true) const { return m_api.playAnimation(m_api.context, node, clip, loop) != 0; }
        bool ShowScreen(const char *screen, bool visible = true, bool overlay = true) const { return m_api.showScreen(m_api.context, screen, visible, overlay) != 0; }
        bool SetQuad(const char *screen, const char *id, const UiQuad &quad) const { return m_api.setQuad(m_api.context, screen, id, &quad) != 0; }
        bool RemoveWidget(const char *screen, const char *id) const { return m_api.removeWidget(m_api.context, screen, id) != 0; }
        bool SurfaceSize(UiSurface &surface) const { return m_api.getSurfaceSize(m_api.context, &surface) != 0; }
        bool WidgetState(const char *screen, const char *id, UiWidgetState &state) const { return m_api.getWidgetState(m_api.context, screen, id, &state) != 0; }
        bool Clicked(const char *screen, const char *id) const
        {
            UiWidgetState state{};
            return WidgetState(screen, id, state) && state.clicked;
        }

    private:
        const ScriptApi &m_api;
    };
} // namespace phasma
