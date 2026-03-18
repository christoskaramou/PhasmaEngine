#pragma once
#include "Widget.h"

struct ImGuiStyle;

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class CommandBuffer;
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

    private:
        void ShowLoadModelMenuItem();
        void ShowLoadSceneMenuItem();
        void ShowSaveSceneMenuItem();
        void ShowSaveSceneMenuItem_Action(); // Opens file selector for Save As
        void OpenLoadSceneDialog();
        void ShowExitMenuItem();
        void DrawExitPopup();
        void Toolbar();
        void Play();
        void Stop();

        bool m_showExitConfirmation = false;
        bool m_showSaveBeforeLoad = false;
        void DrawSaveBeforeLoadPopup();
        bool m_showSaveBeforeNew = false;
        void DrawSaveBeforeNewPopup();
        void NewScene();
        void SaveEditorConfig();
        void LoadEditorConfig();

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

        // Codebase indexing state
        std::atomic<bool> m_isIndexing{false};
        std::atomic<int> m_indexProgress{0};
        std::atomic<int> m_indexTotal{0};
        std::string m_indexCurrentFile;
        std::mutex m_indexMutex;
        std::atomic<bool> m_indexCancel{false};
        std::atomic<int> m_indexActiveThreads{0};
        int m_indexTotalThreads = 0;
        void *m_indexerPtr = nullptr; // pagent::CodebaseIndexer*, guarded by m_indexMutex
        std::thread m_indexThread;

    public:
        void StartCodebaseIndexing();
        void CancelCodebaseIndexing();
        bool IsIndexing() const { return m_isIndexing.load(); }
        int GetIndexProgress() const { return m_indexProgress.load(); }
        int GetIndexTotal() const { return m_indexTotal.load(); }
        int GetIndexActiveThreads() const { return m_indexActiveThreads.load(); }
        int GetIndexTotalThreads() const { return m_indexTotalThreads; }
        std::string GetIndexCurrentFile() const
        {
            std::lock_guard lock(const_cast<std::mutex &>(m_indexMutex));
            return m_indexCurrentFile;
        }
    };
} // namespace pe
