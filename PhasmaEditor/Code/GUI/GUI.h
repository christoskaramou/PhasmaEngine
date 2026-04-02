#pragma once
#include "PhasmaAgent/CodebaseContext.h"
#include "Widget.h"

struct ImGuiStyle;

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class CommandBuffer;
    class EditorToolServer;
    class EditorToolRuntime;
    class Queue;
    class Image;

    class GUI
    {
    public:
        GUI();
        ~GUI();

        void Init();
        void Update();
        void ExecutePass(CommandBuffer *cmd);
        void DrawPlatformWindows();
        bool Render() const { return m_render; }
        void ToggleRender() { m_render = !m_render; }
        void TriggerExitConfirmation();
        void RequestDockReset() { m_requestDockReset = true; }
        void NotifyChange()
        {
            m_needIdleCapture = true;
            m_idleFramesAfterEdit = 0;
        }
        void QueueMainThreadAction(std::function<void()> fn);
        EditorToolRuntime *GetEditorToolRuntime() const { return m_editorToolRuntime.get(); }
        bool IsMcpServerRunning() const;
        pagent::CodebaseIndexStatus GetCodebaseStatus() const { return m_codebase.GetStatus(); }
        std::shared_ptr<pagent::BM25Index> GetCodebaseBM25Shared() const { return m_codebase.GetCodebaseBM25Shared(); }
        void SetMcpServerEnabled(bool enabled);

        // Called after the window is shown to apply the correct layout
        void ApplyStartupLayout();

        // Thread-safe GpuTimer access
        std::vector<GpuTimerSample> PopGpuTimerInfos();

        uint32_t GetDockspaceId() const { return m_dockspaceId; }

        template <typename T>
        T *GetWidget()
        {
            for (auto &widget : m_widgets)
            {
                if (auto *p = dynamic_cast<T *>(widget.get()))
                    return p;
            }
            return nullptr;
        }

        void StartCodebaseIndexing();
        void StartCodebaseIndexing(bool fullRebuild);
        void CancelCodebaseIndexing();
        bool HasCodebaseIndex() const;
        size_t GetCodebaseEntryCount() const;
        bool IsIndexing() const { return m_isIndexing.load(); }
        int GetIndexProgress() const { return m_indexProgress.load(); }
        int GetIndexTotal() const { return m_indexTotal.load(); }
        std::string GetIndexCurrentFile() const
        {
            std::lock_guard lock(m_indexMutex);
            return m_indexCurrentFile;
        }

    private:
        friend class Hierarchy;

        void ShowLoadModelMenuItem();
        void ShowLoadSceneMenuItem();
        void ShowSaveSceneMenuItem();
        void ShowSaveSceneMenuItem_Action(); // Opens file selector for Save As
        void OpenLoadSceneDialog();
        void ShowExitMenuItem();
        void ShowRunScriptTestsMenu();
        void RunScriptTests(const std::vector<std::string> &paths, const std::string &label);
        void DrawExitPopup();
        void Toolbar();
        void Play();
        void Stop();
        void NewScene();
        void SaveEditorConfig();
        void LoadEditorConfig();
        void LoadAgentConfig();

        bool m_showExitConfirmation = false;
        bool m_showSaveBeforeLoad = false;
        void DrawSaveBeforeLoadPopup();
        bool m_showSaveBeforeNew = false;
        void DrawSaveBeforeNewPopup();

        bool m_showOverwriteConfirmation = false;
        std::filesystem::path m_pendingSavePath;
        bool m_exitAfterSave = false; // set when Save & Exit is chosen with no existing path
        void DrawOverwriteConfirmationPopup();

        void Menu();
        void StatusBar();
        void BuildDockspace();
        void ResetDockspaceLayout(uint32_t dockspaceId);

        bool m_render;
        std::unique_ptr<Attachment> m_attachment;
        bool m_show_demo_window;
        uint32_t m_dockspaceId;
        bool m_dockspaceInitialized;
        bool m_requestDockReset;
        bool m_hasIniFile = false;

        // Undo/Redo state tracking
        bool m_wasAnyItemActive = false;
        bool m_needIdleCapture = true;
        int m_idleFramesAfterEdit = 0;
        std::vector<GpuTimerSample> m_gpuTimerInfos;
        std::mutex m_timerMutex;

        std::vector<std::shared_ptr<Widget>> m_widgets;
        std::vector<std::shared_ptr<Widget>> m_menuWindowWidgets;
        std::mutex m_mainThreadActionMutex;
        std::vector<std::function<void()>> m_pendingMainThreadActions;
        std::unique_ptr<EditorToolRuntime> m_editorToolRuntime;
        std::unique_ptr<EditorToolServer> m_editorToolServer;
        std::shared_ptr<std::atomic<bool>> m_codebaseAlive = std::make_shared<std::atomic<bool>>(true);
        pagent::CodebaseContext m_codebase;
        bool m_mcpStartEnabled = false;

        // Codebase indexing state
        std::atomic<bool> m_isIndexing{false};
        std::atomic<int> m_indexProgress{0};
        std::atomic<int> m_indexTotal{0};
        std::string m_indexCurrentFile;
        mutable std::mutex m_indexMutex;
        std::atomic<bool> m_indexCancel{false};
        void *m_indexerPtr = nullptr; // pagent::CodebaseIndexer*, guarded by m_indexMutex
        std::thread m_indexThread;
        std::string m_playModeSnapshot;
    };
} // namespace pe
