#pragma once

namespace pe
{
    class Scene;

    class UndoRedo
    {
    public:
        static UndoRedo &Instance();

        // Capture current state and compare with the previous idle snapshot.
        // If different, push the previous snapshot as an undo entry.
        void CaptureIdleState(Scene &scene);

        // Explicitly save the current scene state as an undo entry.
        // Call this before destructive actions (delete node/mesh/model/light/etc.)
        // that bypass the idle-capture mechanism.
        void RecordSnapshot(Scene &scene);

        void Undo(Scene &scene);
        void Redo(Scene &scene);

        bool CanUndo() const { return !m_undoStack.empty(); }
        bool CanRedo() const { return !m_redoStack.empty(); }
        void Clear();

    private:
        UndoRedo() = default;

        std::deque<std::string> m_undoStack;
        std::deque<std::string> m_redoStack;
        std::string m_idleSnapshot;
        bool m_hasIdleSnapshot = false;
        int m_settleFrames = 0;

        static constexpr size_t MAX_HISTORY = 100;
        static constexpr int SETTLE_FRAMES = 3;
    };
} // namespace pe
