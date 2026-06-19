#pragma once

#ifdef PE_PHYSICS2D

#include "GUI/Widget.h"

namespace pe
{
    struct NodeId;
    class Scene;

    class Physics2DWidget : public Widget
    {
    public:
        Physics2DWidget() : Widget("Physics2DWidget") {}
        void DrawEmbed(NodeId *node, Scene *scene);
    };
} // namespace pe

#endif // PE_PHYSICS2D
