#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "Runtime/PlayerHost.h"
#include <SDL.h>

extern "C" int SDL_main(int argc, char *argv[])
{
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");

    pe::PlayerHostDesc desc{};
    desc.runtimeUiBackendFactory = pe::CreateImGuiRuntimeUiBackend;
    return pe::RunPlayerHost(argc, argv, std::move(desc));
}
