#pragma once

namespace pe
{
    class Scene;

    struct HistoryEntry
    {
        std::string snapshot; // JSON blob
        std::string label;    // e.g. "Deleted Cube", "Moved Sphere"
    };

    class UndoRedo
    {
    public:
        static UndoRedo &Instance();

        void CaptureIdleState(Scene &scene);
        void RecordSnapshot(Scene &scene, std::string label = "Scene Change");

        void Undo(Scene &scene);
        void Redo(Scene &scene);

        // Jump back stepsBack entries (stepsBack=1 == normal Undo)
        void UndoTo(Scene &scene, size_t stepsBack);
        // Jump forward stepsForward entries (stepsForward=1 == normal Redo)
        void RedoTo(Scene &scene, size_t stepsForward);

        bool CanUndo() const { return !m_undoStack.empty(); }
        bool CanRedo() const { return !m_redoStack.empty(); }

        const std::deque<HistoryEntry> &GetUndoStack() const { return m_undoStack; }
        const std::deque<HistoryEntry> &GetRedoStack() const { return m_redoStack; }

        void Clear();

    private:
        UndoRedo() = default;

        void PushUndo(HistoryEntry entry);
        void PushRedo(HistoryEntry entry);
        bool RestoreEntry(Scene &scene, const HistoryEntry &entry);

        std::deque<HistoryEntry> m_undoStack;
        std::deque<HistoryEntry> m_redoStack;
        std::string m_idleSnapshot;
        bool m_hasIdleSnapshot = false;
        bool m_restoring = false;
        int m_settleFrames = 0;

        static constexpr size_t MAX_HISTORY = 100;
        static constexpr int SETTLE_FRAMES = 3;
    };
} // namespace pe
