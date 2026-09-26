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
    inline constexpr uint32_t ScriptAbiVersion = 5;
    inline constexpr uint32_t ScriptAbiMinVersion = 5; // v4 modules demand an exact host match
    using Node = uint64_t;
    struct Vec3
    {
        float x, y, z;
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

    private:
        const ScriptApi &m_api;
    };
} // namespace phasma
