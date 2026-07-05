#pragma once
#include "Base/Math.h"
#include "GUI/Widget.h"
#include "imgui/imgui.h"

namespace pe
{
    class Camera;

    // Viewport terrain brush: sculpt (raise/dig/smooth/flatten) and scatter-paint the Terrain node
    // directly in the Scene view. This window holds the tool settings; SceneView calls
    // HandleViewport each frame, which raycasts the terrain under the mouse, draws a ring decal and
    // applies strokes as deferred brush ops (QueueSculpt / QueueLevel — the safe mid-frame entry),
    // so edits ride the streamed in-place tile updates with no rebuild hitches.
    class TerrainBrush : public Widget
    {
    public:
        enum class Mode
        {
            Raise = 0, // union sphere at the hit (Shift digs)
            Dig,       // subtract sphere at the hit — the ray hit is 3D, so digging into a cliff
                       // face undercuts it and digging a cave ceiling opens it upward
            Smooth,    // level toward the local surface average (strength = weight per stamp)
            Flatten,   // level toward the height under the stroke start
            Scatter,   // plant/erase scatter meshes through the Map Painter's scatter layer
        };

        TerrainBrush();
        void Update() override;

        // Tool on + a live terrain world present (cheap; SceneView checks it every frame).
        bool Active() const;
        // Feed the viewport state while SceneView draws. Returns true when the brush consumed the
        // mouse (SceneView skips object picking). Must be called between the SceneView window's
        // Begin/End — the decal draws into the current window draw list.
        bool HandleViewport(Camera *camera, const ImVec2 &mouse, const ImVec2 &imageMin,
                            const ImVec2 &imageSize, bool imageHovered);

    private:
        bool m_active = false;
        int m_mode = 0;
        float m_radius = 6.0f;   // metres
        float m_strength = 0.4f; // smooth/flatten weight per stamp
        int m_scatterKind = 0;   // combo index: 0..N-1 = kind 1..N, N = Erase
        bool m_haveFlattenY = false;
        float m_flattenY = 0.0f;
        bool m_haveLastStamp = false;
        vec3 m_lastStamp = vec3(0.0f);
        std::string m_hint; // last stroke problem, shown in the tool window
    };
} // namespace pe
