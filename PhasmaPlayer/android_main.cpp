#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "Runtime/PlayerHost.h"

extern "C" int SDL_main(int argc, char *argv[])
{
    pe::PlayerHostDesc desc{};
    desc.runtimeUiBackendFactory = pe::CreateImGuiRuntimeUiBackend;
    return pe::RunPlayerHost(argc, argv, std::move(desc));
}
