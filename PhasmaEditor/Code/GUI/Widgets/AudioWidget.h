#pragma once

#ifdef PE_AUDIO

#include "GUI/Widget.h"

namespace pe
{
    struct NodeId;
    class Scene;

    class AudioWidget : public Widget
    {
    public:
        AudioWidget() : Widget("AudioWidget") {}
        void DrawEmbed(NodeId *node, Scene *scene);
    };
} // namespace pe

#endif // PE_AUDIO
