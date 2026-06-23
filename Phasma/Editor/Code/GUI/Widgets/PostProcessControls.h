#pragma once

namespace pe
{
    struct PostProcessProfile;

    // Draws the post-process effect controls for one profile. Shared by the PostProcessVolume
    // inspector and the global/scene settings editor. Returns true if any value changed.
    bool DrawPostProcessControls(PostProcessProfile &pp);
} // namespace pe
