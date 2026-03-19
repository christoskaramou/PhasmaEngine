#include "UndoRedo.h"
#include "Scene/Scene.h"

namespace pe
{
    UndoRedo &UndoRedo::Instance()
    {
        static UndoRedo instance;
        return instance;
    }

    void UndoRedo::CaptureIdleState(Scene &scene)
    {
        // Active camera position/euler are always excluded from snapshots
        // so WASD navigation never creates undo entries and is never undone.
        // Camera FOV, near, far, speed are still tracked.
        std::string current = scene.TakeSnapshot();

        if (m_hasIdleSnapshot && m_settleFrames == 0 && current != m_idleSnapshot)
        {
            m_undoStack.push_back(std::move(m_idleSnapshot));
            m_redoStack.clear();
            scene.MarkDirty();

            if (m_undoStack.size() > MAX_HISTORY)
                m_undoStack.pop_front();
        }

        if (m_settleFrames > 0)
            --m_settleFrames;

        m_idleSnapshot = std::move(current);
        m_hasIdleSnapshot = true;
    }

    void UndoRedo::RecordSnapshot(Scene &scene)
    {
        std::string current = scene.TakeSnapshot();

        m_undoStack.push_back(std::move(current));
        m_redoStack.clear();
        scene.MarkDirty();

        if (m_undoStack.size() > MAX_HISTORY)
            m_undoStack.pop_front();

        // Reset idle state so next capture starts fresh after the action
        m_hasIdleSnapshot = false;
    }

    void UndoRedo::Undo(Scene &scene)
    {
        if (m_undoStack.empty())
            return;

        m_redoStack.push_back(scene.TakeSnapshot());

        std::string snapshot = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        scene.RestoreSnapshot(snapshot);
        scene.MarkDirty();

        // Don't fix the idle snapshot here — systems like LightSystem run *after*
        // RestoreSnapshot and will alter the scene before the next CaptureIdleState.
        // Clearing hasIdleSnapshot lets the next capture establish a fresh baseline.
        m_hasIdleSnapshot = false;
    }

    void UndoRedo::Redo(Scene &scene)
    {
        if (m_redoStack.empty())
            return;

        m_undoStack.push_back(scene.TakeSnapshot());

        std::string snapshot = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        scene.RestoreSnapshot(snapshot);
        scene.MarkDirty();

        m_hasIdleSnapshot = false;
    }

    void UndoRedo::Clear()
    {
        m_undoStack.clear();
        m_redoStack.clear();
        m_idleSnapshot.clear();
        m_hasIdleSnapshot = false;
        m_settleFrames = SETTLE_FRAMES;
    }
} // namespace pe
