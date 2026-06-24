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
        // showAutoplay: false inside a Trigger Zone (the zone starts/stops the source on enter/exit,
        // so Autoplay is meaningless there); true for a plain node audio source.
        void DrawEmbed(NodeId *node, Scene *scene, bool showAutoplay = true);
    };
} // namespace pe

#endif // PE_AUDIO
