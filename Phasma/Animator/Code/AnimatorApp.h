#pragma once

#include "API/Command.h"
#include "AnimationTimeline.h"
#include "Render/RuntimeSceneRenderer.h"
#include "Scene/Scene.h"

namespace pe
{
    class Image;

    // PhasmaAnimator: one window, one scene (camera, light, grid, a ground plane) and one character. The Animation
    // Timeline (Rig | Animate, its own viewport with the orbit camera) fills everything under the menu bar; the app
    // owns the runtime scene renderer, the ImGui context drawn into the display target after the scene, the .pemesh
    // being animated, a config (last model, recent files, cameras, bookmarks, look) and a command file for tests
    // that cannot use the mouse.
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
        // The unsaved-changes guard: the menu, Recent, a dropped file and the window close button come through
        // here and ask first when the clips or the rig changed. The command file never asks: probes end with
        // animator.exit right after posing.
        void RequestOpen(const std::filesystem::path &pemesh);
        void RequestQuit();
        // The clips back into the .pemesh and the rig document into its rig.json; false carries the first error.
        bool SaveAll(std::string *error = nullptr);
        // An empty scene: the camera, a directional light, the grid and the ground plane.
        void ResetScene();
        // animator.* / timeline.* / rig.* actions (the command file and the menu share them); JSON in, JSON out.
        std::string HandleAction(const std::string &action, const std::string &argsJson);

    private:
        struct LogLine
        {
            LogType type;
            std::string message;
        };
        struct Bookmark
        {
            std::string name;
            OrbitView view;
        };

        bool ProcessEvents();
        bool WindowRenderable() const;
        void ApplyPendingResize();
        void DrawShell();
        void DrawPrompts(); // the unsaved-changes and bookmark-name modals, the Hotkeys window
        void DrawOverlay(CommandBuffer *cmd, Image *displayRT);
        void PollCommandFile();
        void OpenDialog();
        void ImportModelDialog();
        void ImportClipDialog();
        void ExportClipDialog();
        void RunClipAction(const char *action, const std::filesystem::path &path);
        bool PickSaveFile(const char *title, const char *filter, const char *extension, std::string &outPath);
        void LoadConfig(int argc, char *argv[]);
        void SaveConfig();
        std::filesystem::path ConfigPath() const;
        void UpdateTitle();
        void RememberRecent(const std::filesystem::path &path);
        void EnsureGround(); // the flat shadow receiver under the origin (View > Ground)
        void SetGroundVisible(bool visible);
        static float ViewportAspect();
        static Scene *ActiveScene();
        static void WaitSceneMutation();

        Scene m_scene;
        RuntimeSceneRenderer m_renderer;
        std::unique_ptr<AnimationTimeline> m_timeline;
        Attachment m_attachment;
        std::filesystem::path m_modelPath;
        std::vector<NodeId *> m_modelRoots; // the opened model's root nodes (the selection follows the first)
        std::vector<std::filesystem::path> m_recent;
        std::vector<Bookmark> m_bookmarks;
        std::map<std::string, OrbitView> m_views; // the last camera per model (generic path)
        std::filesystem::path m_pendingOpen;      // waits behind the unsaved-changes prompt
        bool m_pendingQuit = false;
        bool m_promptPending = false;
        bool m_bookmarkPromptPending = false;
        bool m_showHotkeys = false;
        bool m_grid = true;
        bool m_ground = true;
        NodeId *m_groundNode = nullptr;
        bool m_quit = false;
        bool m_resizePending = false;
        bool m_openPopupPending = false;
        bool m_clipPopupPending = false;
        bool m_showNotice = false;
        char m_openBuffer[1024] = {};
        char m_clipPathBuffer[1024] = {};
        char m_bookmarkName[64] = {};
        std::string m_clipPopupAction;
        std::string m_notice;
        std::string m_title;
        std::deque<LogLine> m_log;
        std::mutex m_logMutex;
        static AnimatorApp *s_instance;
    };

    // The renderer the animation widgets draw into and wait on (the scene, ResetTAAHistory, WaitAllFramesCommands).
    RuntimeSceneRenderer *GetAnimatorRenderer();
} // namespace pe
