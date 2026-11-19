#pragma once

namespace pe
{
    struct RuntimeUiZoomRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        bool eligible = false; // a visible script quad; the rest keep their place
        bool holds = false;    // a visible panel that keeps its place: quads inside it stay with it
        bool content = false;  // false: a plain decoration (no text, no input) that has moved on screen
    };

    // Where a quad goes: x' = x * scale + offsetX, y' = y * scale + offsetY, and its size and text scale by `scale`.
    struct RuntimeUiZoomPlacement
    {
        float scale = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float textScale = 1.0f; // on top of `scale`: a group that had no room to grow still gets larger text
        float textFit = 1.0f;   // on top of `textScale`: the rest of the zoom, for text that still fits its quad
    };

    // Enlarges one overlay screen's quads for a phone-sized display, where a HUD laid out as a fraction of the
    // surface is physically small. Games size and place their own quads, so this works on the result: quads that
    // overlap or nearly touch form a group (a panel with its text, a row of buttons), and each group grows about
    // its own centre by up to `zoom`, then moves back inside the surface. Groups that would overlap shrink, each only
    // as far as it must, so a crowded corner does not hold the rest back. Quads covering most of the surface (backdrops,
    // click catchers) stay as they are.
    // ponytail: recomputed every frame with no smoothing, so a group can shift when a quad joins or leaves it, and
    // neighbouring groups can end at different sizes. Smooth the zoom over time if that shows.
    std::vector<RuntimeUiZoomPlacement> ZoomRuntimeUiQuads(const std::vector<RuntimeUiZoomRect> &rects,
                                                           float width,
                                                           float height,
                                                           float zoom);
} // namespace pe
