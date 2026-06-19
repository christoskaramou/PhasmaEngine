#pragma once
#include "GUI/Widget.h"

namespace pe
{
    // Authoring panel for the active scene's script manifest (scene_scripts in the .pescene):
    // the on_play script list and the named-action table. Edits mutate the live manifest and
    // mark the scene dirty; Save Scene persists them. Hidden by default — toggle from the
    // window menu.
    class SceneScripts : public Widget
    {
    public:
        SceneScripts();
        void Update() override;
    };
} // namespace pe
