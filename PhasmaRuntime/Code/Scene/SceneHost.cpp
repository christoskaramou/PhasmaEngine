#include "Scene/SceneHost.h"

#include "Base/Log.h"
#include "Scene/SceneAccess.h"
#include "Scene/Scene.h"

#include <memory>
#include <utility>

namespace pe
{
    namespace
    {
        // PhasmaRuntime is static-linked into the editor host and hot-reload module,
        // so these callbacks are per image. Register from the image that calls them.
        // TODO(shared-runtime): move this state behind the shared runtime image if
        // PhasmaRuntime stops being linked as a static library.
        SceneHostCallbacks s_sceneHostCallbacks;

        void WarnMissingSceneHost(const char *operation)
        {
            PE_WARN("[Runtime] Scene host is unavailable for %s", operation);
        }

        Scene *GetHostScene(const char *operation)
        {
            Scene *scene = GetActiveScene();
            if (!scene)
                WarnMissingSceneHost(operation);
            return scene;
        }

        void SyncBeforeMutation()
        {
            if (s_sceneHostCallbacks.beforeMutation)
                s_sceneHostCallbacks.beforeMutation();
        }

        void DestroyDefaultScenePreload(void *payload)
        {
            delete static_cast<Scene::ScenePreload *>(payload);
        }
    } // namespace

    ScenePreloadHandle::ScenePreloadHandle(void *payload, bool valid, ScenePreloadDestroyFn destroy)
        : m_payload(payload), m_valid(valid), m_destroy(destroy)
    {
    }

    ScenePreloadHandle::~ScenePreloadHandle()
    {
        Reset();
    }

    ScenePreloadHandle::ScenePreloadHandle(ScenePreloadHandle &&other) noexcept
        : m_payload(other.m_payload), m_valid(other.m_valid), m_destroy(other.m_destroy)
    {
        other.m_payload = nullptr;
        other.m_valid = false;
        other.m_destroy = nullptr;
    }

    ScenePreloadHandle &ScenePreloadHandle::operator=(ScenePreloadHandle &&other) noexcept
    {
        if (this == &other)
            return *this;

        Reset();
        m_payload = other.m_payload;
        m_valid = other.m_valid;
        m_destroy = other.m_destroy;

        other.m_payload = nullptr;
        other.m_valid = false;
        other.m_destroy = nullptr;
        return *this;
    }

    void *ScenePreloadHandle::ReleasePayload()
    {
        void *payload = m_payload;
        m_payload = nullptr;
        m_valid = false;
        m_destroy = nullptr;
        return payload;
    }

    void ScenePreloadHandle::Reset()
    {
        if (m_payload && m_destroy)
            m_destroy(m_payload);
        m_payload = nullptr;
        m_valid = false;
        m_destroy = nullptr;
    }

    void SetSceneHostCallbacks(SceneHostCallbacks callbacks)
    {
        s_sceneHostCallbacks = callbacks;
    }

    void NewScene()
    {
        if (s_sceneHostCallbacks.newScene)
        {
            s_sceneHostCallbacks.newScene();
            return;
        }

        SyncBeforeMutation();
        if (Scene *scene = GetHostScene("NewScene"))
            scene->NewScene();
    }

    void SaveScene(const std::filesystem::path &path)
    {
        if (s_sceneHostCallbacks.saveScene)
        {
            s_sceneHostCallbacks.saveScene(path);
            return;
        }

        if (Scene *scene = GetHostScene("SaveScene"))
            scene->SaveScene(path);
    }

    void LoadScene(const std::filesystem::path &path)
    {
        if (s_sceneHostCallbacks.loadScene)
        {
            s_sceneHostCallbacks.loadScene(path);
            return;
        }

        SyncBeforeMutation();
        if (Scene *scene = GetHostScene("LoadScene"))
            scene->LoadScene(path);
    }

    ScenePreloadHandle PreloadScene(const std::filesystem::path &path)
    {
        if (s_sceneHostCallbacks.preloadScene)
        {
            return s_sceneHostCallbacks.preloadScene(path);
        }

        auto *preload = new Scene::ScenePreload(Scene::PreloadScene(path));
        return ScenePreloadHandle(preload, preload->valid, DestroyDefaultScenePreload);
    }

    void LoadSceneApply(ScenePreloadHandle preload)
    {
        if (s_sceneHostCallbacks.loadSceneApply)
        {
            s_sceneHostCallbacks.loadSceneApply(std::move(preload));
            return;
        }

        std::unique_ptr<Scene::ScenePreload> scenePreload(
            static_cast<Scene::ScenePreload *>(preload.ReleasePayload()));
        if (!scenePreload || !scenePreload->valid)
            return;

        SyncBeforeMutation();
        if (Scene *scene = GetHostScene("LoadSceneApply"))
            scene->LoadSceneApply(std::move(*scenePreload));
    }
} // namespace pe
