#pragma once

#include <filesystem>

namespace pe
{
    class Scene;

    using ScenePreloadDestroyFn = void (*)(void *);
    using SceneHostBeforeMutationFn = void (*)();

    class ScenePreloadHandle
    {
    public:
        ScenePreloadHandle() = default;
        ScenePreloadHandle(void *payload, bool valid, ScenePreloadDestroyFn destroy);
        ~ScenePreloadHandle();

        ScenePreloadHandle(const ScenePreloadHandle &) = delete;
        ScenePreloadHandle &operator=(const ScenePreloadHandle &) = delete;
        ScenePreloadHandle(ScenePreloadHandle &&other) noexcept;
        ScenePreloadHandle &operator=(ScenePreloadHandle &&other) noexcept;

        [[nodiscard]] bool IsValid() const { return m_valid; }
        [[nodiscard]] void *GetPayload() const { return m_payload; }
        [[nodiscard]] void *ReleasePayload();
        void Reset();

    private:
        void *m_payload = nullptr;
        bool m_valid = false;
        ScenePreloadDestroyFn m_destroy = nullptr;
    };

    struct SceneHostCallbacks
    {
        SceneHostBeforeMutationFn beforeMutation = nullptr;
        void (*newScene)() = nullptr;
        void (*saveScene)(const std::filesystem::path &path) = nullptr;
        void (*loadScene)(const std::filesystem::path &path) = nullptr;
        ScenePreloadHandle (*preloadScene)(const std::filesystem::path &path) = nullptr;
        void (*loadSceneApply)(ScenePreloadHandle preload) = nullptr;
    };

    void SetSceneHostCallbacks(SceneHostCallbacks callbacks);
    void SyncSceneBeforeMutation();
    void NewScene();
    void SaveScene(const std::filesystem::path &path);
    void LoadScene(const std::filesystem::path &path);
    [[nodiscard]] ScenePreloadHandle PreloadScene(const std::filesystem::path &path);
    void LoadSceneApply(ScenePreloadHandle preload);
} // namespace pe
