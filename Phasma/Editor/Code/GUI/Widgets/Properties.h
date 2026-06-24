#pragma once
#include "GUI/Widget.h"
#include "Scene/SelectionManager.h"

namespace pe
{
    class Properties : public Widget
    {
    public:
        Properties() : Widget("Properties") {}
        void Update() override;

    private:
        // Inspector "pin" (lock): when active, the panel keeps showing this captured selection even as the
        // global selection changes — until unpinned or the pinned node is deleted.
        struct PinnedSelection
        {
            bool active = false;
            SelectionType type = SelectionType::Node;
            NodeId *node = nullptr;
            LightType lightType = LightType::Directional;
            int lightIndex = -1;
            int emitterIndex = -1;
            int cameraIndex = -1;
        };
        PinnedSelection m_pin;
    };
} // namespace pe
