#ifndef DOF_H_
#define DOF_H_

// Thin-lens depth of field, gathered in one pass (Gustafsson's scatter-as-gather, "Bokeh depth of field in a single
// pass", 2018): a sample spreads over this pixel when its own circle of confusion reaches it. DOFPrepassCS packs each
// pixel's colour with its inverse depth and finds every tile's largest blur, DOFTileDilateCS spreads that over the
// blur cap, and DOFPS gathers only as far as it reaches.
struct DOFParams
{
    float maxRadius;     // largest blur radius, pixels (post-process dof_blur_range)
    float blend;
    float focusDistance; // metres along the view direction; 0 focuses on the screen centre
    float nearPlane;
    float fStop;
    float focalLength;   // metres, from the vertical field of view and the sensor height
    float sensorHeight;  // metres
    float imageHeight;   // pixels
    uint flags;          // DOF_PERSPECTIVE, DOF_TRANSPARENT_DEPTH
};

static const uint DOF_PERSPECTIVE = 1u;
static const uint DOF_TRANSPARENT_DEPTH = 2u; // the MaterialX forward pass's surface depth is valid this frame

#define DOF_TILE 16 // pixels per tile side, DOFPass kTileSize

// The focus as an inverse depth: the camera's focus distance, or the screen centre's depth when that is 0.
float InverseFocus(float centreInverseDepth, DOFParams p)
{
    return p.focusDistance > 0.0 ? 1.0 / p.focusDistance : centreInverseDepth;
}

// Circle of confusion radius in pixels per unit of |1/z - 1/z_focus|: the diameter on the sensor is
// f^2 * z_focus * |1/z_focus - 1/z| / (N * (z_focus - f)), halved and scaled from the sensor to the image.
float BlurPerInverseDepth(float inverseFocus, DOFParams p)
{
    const float f = p.focalLength;
    const float zf = max(1.0 / max(inverseFocus, 1.0e-6), 1.001 * f);
    return 0.5 * f * f * zf / (p.fStop * (zf - f)) / p.sensorHeight * p.imageHeight;
}

float BlurRadius(float inverseDepth, float inverseFocus, float scale, DOFParams p)
{
    return min(abs(inverseDepth - inverseFocus) * scale, p.maxRadius);
}

#endif
