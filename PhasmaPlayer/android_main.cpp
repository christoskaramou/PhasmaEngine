#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "Runtime/PlayerHost.h"
#include <SDL.h>

extern "C" int SDL_main(int argc, char *argv[])
{
    // Lock SDL to landscape. Without this, SDL's setOrientationBis requests
    // SCREEN_ORIENTATION_FULL_USER and overrides the manifest's sensorLandscape,
    // letting the surface flip to portrait (1220x2712). The runtime UI then
    // letterboxes to a tiny vh=549 band and the landscape-authored 3D scene gets
    // squished out of view. Keep this in sync with AndroidManifest screenOrientation.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

    pe::PlayerHostDesc desc{};
    desc.runtimeUiBackendFactory = pe::CreateImGuiRuntimeUiBackend;
    return pe::RunPlayerHost(argc, argv, std::move(desc));
}
