#pragma once

namespace pe
{
    class Scene;
    class Image;

    // One visible node in a decoded camera view. Aggregates every draw mapped to the node:
    // exact pixel coverage, the screen-space bounding box, the nearest visible NDC depth and
    // the camera-space distance to that nearest surface.
    struct CameraViewNodeVis
    {
        int nodeIndex = -1;
        uint64_t visiblePixels = 0;
        uint32_t minX = 0;
        uint32_t minY = 0;
        uint32_t maxX = 0;
        uint32_t maxY = 0;
        float nearestNdcDepth = 0.0f;
        float distance = 0.0f; // +inf when not finite (sky / behind camera)
    };

    // Result of DecodeCameraView — the occlusion-exact set of nodes the active camera sees,
    // sorted by visiblePixels descending. `valid == false` carries a human-readable `error`.
    struct CameraViewResult
    {
        bool valid = false;
        std::string error;
        uint32_t width = 0;
        uint32_t height = 0;
        vec3 cameraPosition = vec3(0.0f);
        mat4 viewProjection = mat4(1.0f);
        std::vector<CameraViewNodeVis> nodes;
    };

    // Result of PickCameraPixel — the node/world point under a single pixel of the decoded view.
    // On a hit, worldHit is the exact unprojected surface point. On a miss (sky/empty) hit is
    // false and groundPoint (when hasGroundPoint) is the camera ray intersected with Y=groundY.
    struct CameraPickResult
    {
        bool valid = false;
        std::string error;
        uint32_t width = 0;
        uint32_t height = 0;
        bool hit = false;
        int nodeIndex = -1;
        float ndcDepth = 0.0f;
        float distance = 0.0f;
        vec3 worldHit = vec3(0.0f);
        bool hasGroundPoint = false;
        vec3 groundPoint = vec3(0.0f);
    };

    // On-demand GPU object-id perception from the scene's ACTIVE camera. Re-rasterizes the live
    // culled draws into an R32G32_UINT id+depth target, depth-tested against the EXISTING depth
    // buffer (GEQUAL under reverse-Z, depth-write off) so only the frontmost surface writes its
    // id => occlusion-exact. DecodeCameraView reduces that to per-node visibility; PickCameraPixel
    // reads a single pixel and unprojects it.
    //
    // Both issue a one-off command submission and block on it, so call on the render/main thread
    // after RendererSystem::WaitAllFramesCommands(). `depth` is the live depth-stencil target.
    CameraViewResult DecodeCameraView(Scene &scene, Image *depth, uint32_t minPixels);
    CameraPickResult PickCameraPixel(Scene &scene, Image *depth, int x, int y, float groundY);
} // namespace pe
