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

        if (m_hasIdleSnapshot && current != m_idleSnapshot)
        {
            m_undoStack.push_back(std::move(m_idleSnapshot));
            m_redoStack.clear();

            if (m_undoStack.size() > MAX_HISTORY)
                m_undoStack.pop_front();
        }

        m_idleSnapshot = std::move(current);
        m_hasIdleSnapshot = true;
    }

    void UndoRedo::Undo(Scene &scene)
    {
        if (m_undoStack.empty())
            return;

        m_redoStack.push_back(scene.TakeSnapshot());

        std::string snapshot = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        scene.RestoreSnapshot(snapshot);

        m_idleSnapshot = std::move(snapshot);
        m_hasIdleSnapshot = true;
    }

    void UndoRedo::Redo(Scene &scene)
    {
        if (m_redoStack.empty())
            return;

        m_undoStack.push_back(scene.TakeSnapshot());

        std::string snapshot = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        scene.RestoreSnapshot(snapshot);

        m_idleSnapshot = std::move(snapshot);
        m_hasIdleSnapshot = true;
    }

    void UndoRedo::Clear()
    {
        m_undoStack.clear();
        m_redoStack.clear();
        m_idleSnapshot.clear();
        m_hasIdleSnapshot = false;
    }
} // namespace pe
