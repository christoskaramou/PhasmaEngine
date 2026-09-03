#pragma once

#include "API/Command.h"
#include "Render/RuntimeSceneRenderer.h"
#include "Scene/Scene.h"

namespace pe
{
    class AnimationTimeline;
    class Image;

    // PhasmaAnimator: one window, one scene (camera, light, grid) and one character. The Animation Timeline
    // (Rig | Animate, its own viewport with the orbit camera) fills everything under the menu bar; the app owns
    // the runtime scene renderer, the ImGui context drawn into the display target after the scene, the .pemesh
    // being animated, a config with the last model, and a command file for tests that cannot use the mouse.
    class AnimatorApp
    {
    public:
        AnimatorApp(int argc, char *argv[]);
        ~AnimatorApp();
        bool Frame();

        static AnimatorApp *Instance() { return s_instance; }
        RuntimeSceneRenderer &Renderer() { return m_renderer; }
        Scene &GetScene() { return m_scene; }
        AnimationTimeline &Timeline() { return *m_timeline; }

        void *RegisterImageTexture(Image *image);
        void ReleaseImageTexture(void *&texture);
        // The sampled copy of the display target the Timeline's viewport shows (GUIState::s_sceneViewImage and
        // s_viewportTextureId), made or re-made at the display size.
        bool EnsureSceneTexture();
        // Native file picker where the platform has one (Windows); false = nothing picked.
        bool PickFile(const char *title, const char *filter, std::string &outPath);
        bool OpenModel(const std::filesystem::path &pemesh, std::string *error = nullptr);
        // An empty scene: the camera, a directional light and the grid.
        void ResetScene();
        // animator.* / timeline.* / rig.* actions (the command file and the menu share them); JSON in, JSON out.
        std::string HandleAction(const std::string &action, const std::string &argsJson);

    private:
        struct LogLine
        {
            LogType type;
            std::string message;
        };

        bool ProcessEvents();
        bool WindowRenderable() const;
        void ApplyPendingResize();
        void DrawShell();
        void DrawOverlay(CommandBuffer *cmd, Image *displayRT);
        void PollCommandFile();
        void OpenDialog();
        void LoadConfig(int argc, char *argv[]);
        void SaveConfig() const;
        std::filesystem::path ConfigPath() const;
        static float ViewportAspect();
        static Scene *ActiveScene();
        static void WaitSceneMutation();

        Scene m_scene;
        RuntimeSceneRenderer m_renderer;
        std::unique_ptr<AnimationTimeline> m_timeline;
        Attachment m_attachment;
        std::filesystem::path m_modelPath;
        std::vector<NodeId *> m_modelRoots; // the opened model's root nodes (the selection follows the first)
        bool m_quit = false;
        bool m_resizePending = false;
        bool m_openPopupPending = false;
        char m_openBuffer[1024] = {};
        std::deque<LogLine> m_log;
        std::mutex m_logMutex;
        static AnimatorApp *s_instance;
    };

    // The renderer the animation widgets draw into and wait on (the scene, ResetTAAHistory, WaitAllFramesCommands).
    RuntimeSceneRenderer *GetAnimatorRenderer();
} // namespace pe
