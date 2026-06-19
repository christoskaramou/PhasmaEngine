#pragma once

#ifdef PE_PHYSICS

#include "GUI/Widget.h"

namespace pe
{
    struct NodeId;
    class Scene;

    class PhysicsWidget : public Widget
    {
    public:
        PhysicsWidget() : Widget("PhysicsWidget") {}
        void DrawEmbed(NodeId *node, Scene *scene);
    };
} // namespace pe

#endif // PE_PHYSICS
